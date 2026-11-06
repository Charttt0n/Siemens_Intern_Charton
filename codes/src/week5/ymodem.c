#include "ymodem.h"
#include "crc16.h"

#define SYM_SOH 0x01u
#define SYM_STX 0x02u
#define SYM_EOT 0x04u
#define SYM_ACK 0x06u
#define SYM_NAK 0x15u
#define SYM_CAN 0x18u
#define SYM_CRC 0x43u /* 'C': receiver requests CRC-16 mode */

#define PACKET_128 128u
#define PACKET_1024 1024u
/* Framing/event bound, not a time source: how much noise one read_frame may
   skip before it reports a bad frame. */
#define JUNK_LIMIT 4096u
/* One maximum-length frame (header + data + CRC) plus margin. */
#define NO_PROGRESS_FRAME_MS (YMODEM_PACKET_MAX + 8u)

#define READ_BYTE_OK 1
#define READ_BYTE_TIMEOUT 0
#define READ_BYTE_IO (-1)
#define READ_BYTE_IDLE (-2) /* no-progress wall-clock budget exhausted */

typedef enum {
    PHASE_BLOCK0 = 0,
    PHASE_DATA,
    PHASE_FINAL
} phase;

typedef struct {
    const ymodem_io *io;
    const ymodem_sink *sink;
    const ymodem_config *config;
    ymodem_stats *stats;
    uint32_t no_progress;  /* consecutive events without accepted new data */
    uint32_t progress_ms;  /* monotonic time of the last accepted progress */
    uint32_t budget_ms;    /* resolved no-progress wall-clock budget */
    uint32_t declared;
    uint32_t received;
    uint32_t blocks;
    uint8_t last_seq;
    int have_last;
    int sink_open;
    char name[YMODEM_NAME_MAX];
} receiver;

typedef struct {
    uint8_t data[YMODEM_PACKET_MAX];
    uint32_t size;
    uint8_t seq;
} frame;

enum {
    RX_FRAME = 0,
    RX_EOT,
    RX_CANCEL,
    RX_TIMEOUT,
    RX_IDLE,
    RX_IO,
    RX_BAD
};

static uint32_t clock_ms(const receiver *r) {
    return r->io->now_ms(r->io->context);
}

static int tx_byte(receiver *r, uint8_t value) {
    /* Strict contract: only 0 means the whole byte was written. */
    return r->io->write(r->io->context, &value, 1) == 0 ? 0 : -1;
}

static int tx_cancel(receiver *r) {
    int result = 0;
    if (tx_byte(r, SYM_CAN)) result = -1;
    if (tx_byte(r, SYM_CAN)) result = -1;
    return result;
}

static int read_byte(receiver *r, uint8_t *out) {
    uint32_t start = clock_ms(r);
    for (;;) {
        /* The budget applies while bytes keep arriving as well, not only to
           silent gaps. Only accepted new data may refresh it. */
        if ((uint32_t)(clock_ms(r) - r->progress_ms) >= r->budget_ms)
            return READ_BYTE_IDLE;
        int count = r->io->read(r->io->context, out, 1);
        if (count < 0) return READ_BYTE_IO;
        if (count > 1) return READ_BYTE_IO; /* illegal count for a 1 byte read */
        if (count == 1) return READ_BYTE_OK;
        if ((uint32_t)(clock_ms(r) - start) >= r->config->timeout_ms)
            return READ_BYTE_TIMEOUT;
    }
}

/* 1 = all bytes read, 0 = timeout, -1 = IO error, -2 = no-progress budget. */
static int read_exact(receiver *r, uint8_t *out, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        int result = read_byte(r, &out[i]);
        if (result <= 0) return result;
    }
    return 1;
}

static int read_frame(receiver *r, frame *f) {
    uint8_t header[2];
    uint32_t junk = 0;
    int result;
    for (;;) {
        result = read_byte(r, &header[0]);
        if (result == READ_BYTE_TIMEOUT) return RX_TIMEOUT;
        if (result == READ_BYTE_IO) return RX_IO;
        if (result == READ_BYTE_IDLE) return RX_IDLE;
        if (header[0] == SYM_EOT) return RX_EOT;
        if (header[0] == SYM_CAN) {
            uint8_t second;
            result = read_byte(r, &second);
            if (result == READ_BYTE_IO) return RX_IO;
            if (result == READ_BYTE_IDLE) return RX_IDLE;
            if (result == READ_BYTE_OK && second == SYM_CAN) return RX_CANCEL;
            return RX_BAD;
        }
        if (header[0] == SYM_SOH) { f->size = PACKET_128; break; }
        if (header[0] == SYM_STX) { f->size = PACKET_1024; break; }
        /* Resynchronise over noise or the tail of an interrupted frame. */
        if (++junk > JUNK_LIMIT) return RX_BAD;
    }
    result = read_exact(r, header, 2);
    if (result == READ_BYTE_IO) return RX_IO;
    if (result == READ_BYTE_IDLE) return RX_IDLE;
    if (result == READ_BYTE_TIMEOUT) return RX_BAD;
    if ((uint8_t)(header[0] + header[1]) != 0xFFu) {
        /* Bad sequence pair: consume the rest of this frame so the next read
           starts at a frame boundary, then ask for a retransmission. IO and
           budget errors must propagate; only a byte timeout is a bad frame.
           After a failed payload drain the CRC tail is not read. */
        uint8_t discard[YMODEM_PACKET_MAX];
        int drain = read_exact(r, discard, f->size);
        if (drain == READ_BYTE_IO) return RX_IO;
        if (drain == READ_BYTE_IDLE) return RX_IDLE;
        if (drain == READ_BYTE_TIMEOUT) return RX_BAD;
        drain = read_exact(r, discard, 2);
        if (drain == READ_BYTE_IO) return RX_IO;
        if (drain == READ_BYTE_IDLE) return RX_IDLE;
        return RX_BAD; /* a timeout of the CRC tail is a bad frame */
    }
    f->seq = header[0];
    result = read_exact(r, f->data, f->size);
    if (result == READ_BYTE_IO) return RX_IO;
    if (result == READ_BYTE_IDLE) return RX_IDLE;
    if (result == READ_BYTE_TIMEOUT) return RX_BAD;
    uint8_t check[2];
    result = read_exact(r, check, 2);
    if (result == READ_BYTE_IO) return RX_IO;
    if (result == READ_BYTE_IDLE) return RX_IDLE;
    if (result == READ_BYTE_TIMEOUT) return RX_BAD;
    uint16_t remote = (uint16_t)(((uint16_t)check[0] << 8) | check[1]);
    if (crc16(f->data, f->size) != remote) return RX_BAD;
    return RX_FRAME;
}

/* Validate block 0 name and decimal length; leaves name/declared in r. */
static int parse_block0(receiver *r, const frame *f) {
    uint32_t name_len = 0;
    while (name_len < f->size && f->data[name_len] != 0) {
        uint8_t c = f->data[name_len];
        if (name_len + 1u >= YMODEM_NAME_MAX) return YMODEM_PROTOCOL;
        if (c < 0x20u || c == 0x7Fu || c == '/' || c == '\\')
            return YMODEM_PROTOCOL;
        r->name[name_len] = (char)c;
        ++name_len;
    }
    if (name_len == 0 || name_len >= f->size) return YMODEM_PROTOCOL;
    r->name[name_len] = 0;
    uint32_t pos = name_len + 1u;
    if (pos >= f->size) return YMODEM_PROTOCOL;
    if (f->data[pos] < '0' || f->data[pos] > '9') return YMODEM_PROTOCOL;
    uint32_t value = 0;
    uint32_t digits = 0;
    while (pos < f->size) {
        uint8_t c = f->data[pos];
        if (c < '0' || c > '9') break;
        uint32_t digit = (uint32_t)(c - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u) return YMODEM_TOO_LARGE;
        value = value * 10u + digit;
        ++digits;
        ++pos;
        if (digits > 10u) return YMODEM_TOO_LARGE;
    }
    if (digits == 0) return YMODEM_PROTOCOL;
    /* A decimal length may be followed by optional space-separated fields. */
    if (pos < f->size && f->data[pos] != 0 && f->data[pos] != ' ')
        return YMODEM_PROTOCOL;
    if (value > r->config->max_file_size) return YMODEM_TOO_LARGE;
    r->declared = value;
    return YMODEM_OK;
}

static void copy_name(ymodem_stats *stats, const char *name) {
    uint32_t i = 0;
    while (i + 1u < YMODEM_NAME_MAX && name[i]) {
        stats->name[i] = name[i];
        ++i;
    }
    stats->name[i] = 0;
}

static int fail(receiver *r, int result) {
    (void)tx_cancel(r);
    if (r->sink_open) {
        if (r->sink->abort) r->sink->abort(r->sink->context);
        r->sink_open = 0;
    }
    return result;
}

/* Accepted block 0 or newly written data restarts the no-progress budget. */
static void note_progress(receiver *r) {
    r->no_progress = 0;
    r->progress_ms = clock_ms(r);
}

/* 0 = still within budget, otherwise the result to return. */
static int no_progress_event(receiver *r, int timed_out) {
    ++r->no_progress;
    if ((uint32_t)(clock_ms(r) - r->progress_ms) >= r->budget_ms)
        return YMODEM_TIMEOUT;
    if (r->no_progress > r->config->retry_limit)
        return timed_out ? YMODEM_TIMEOUT : YMODEM_RETRY_LIMIT;
    return 0;
}

static int send_request(receiver *r, uint8_t value) {
    if (r->stats) ++r->stats->retries;
    return tx_byte(r, value);
}

int ymodem_receive(const ymodem_io *io, const ymodem_sink *sink,
                   const ymodem_config *config, ymodem_stats *stats) {
    if (!io || !sink || !config || !io->read || !io->write || !io->now_ms)
        return YMODEM_ARGUMENT;
    if (!sink->begin || !sink->write || !sink->finish)
        return YMODEM_ARGUMENT;
    if (!config->max_file_size || !config->timeout_ms || !config->retry_limit)
        return YMODEM_ARGUMENT;
    receiver r;
    r.io = io;
    r.sink = sink;
    r.config = config;
    r.stats = stats;
    r.no_progress = 0;
    r.declared = 0;
    r.received = 0;
    r.blocks = 0;
    r.last_seq = 0;
    r.have_last = 0;
    r.sink_open = 0;
    for (uint32_t i = 0; i < YMODEM_NAME_MAX; ++i) r.name[i] = 0;
    r.budget_ms = config->no_progress_timeout_ms;
    if (!r.budget_ms) {
        uint32_t factor = NO_PROGRESS_FRAME_MS;
        r.budget_ms = config->timeout_ms > UINT32_MAX / factor
                          ? UINT32_MAX
                          : config->timeout_ms * factor;
    }
    if (stats) {
        stats->name[0] = 0;
        stats->size = 0;
        stats->received = 0;
        stats->blocks = 0;
        stats->retries = 0;
    }
    if (tx_byte(&r, SYM_CRC)) return YMODEM_IO;
    note_progress(&r);
    phase current = PHASE_BLOCK0;
    frame f;
    for (;;) {
        int rx = read_frame(&r, &f);
        if (rx == RX_IO) return fail(&r, YMODEM_IO);
        if (rx == RX_CANCEL) return fail(&r, YMODEM_CANCELLED);
        if (rx == RX_IDLE) return fail(&r, YMODEM_TIMEOUT);
        if (rx == RX_TIMEOUT || rx == RX_BAD) {
            int limit = no_progress_event(&r, rx == RX_TIMEOUT);
            if (limit) return fail(&r, limit);
            if (send_request(&r, current == PHASE_DATA ? SYM_NAK : SYM_CRC))
                return fail(&r, YMODEM_IO);
            continue;
        }
        if (rx == RX_EOT) {
            if (current == PHASE_BLOCK0) return fail(&r, YMODEM_PROTOCOL);
            if (r.received != r.declared) return fail(&r, YMODEM_UNDERFLOW);
            if (current == PHASE_FINAL) {
                /* A repeated EOT is no progress and must not refresh the
                   budget; only block 0 or new data may do that. */
                int limit = no_progress_event(&r, 0);
                if (limit) return fail(&r, limit);
            }
            if (tx_byte(&r, SYM_ACK)) return fail(&r, YMODEM_IO);
            if (tx_byte(&r, SYM_CRC)) return fail(&r, YMODEM_IO);
            current = PHASE_FINAL;
            continue;
        }
        if (current == PHASE_BLOCK0) {
            if (f.seq != 0) {
                int limit = no_progress_event(&r, 0);
                if (limit) return fail(&r, limit);
                if (send_request(&r, SYM_NAK)) return fail(&r, YMODEM_IO);
                continue;
            }
            if (f.data[0] == 0) {
                /* Empty block 0: the sender has nothing to transfer. */
                if (tx_byte(&r, SYM_ACK)) return fail(&r, YMODEM_IO);
                return YMODEM_OK;
            }
            int parsed = parse_block0(&r, &f);
            if (parsed == YMODEM_TOO_LARGE) return fail(&r, YMODEM_TOO_LARGE);
            if (parsed != YMODEM_OK) return fail(&r, YMODEM_PROTOCOL);
            if (r.sink->begin(r.sink->context, r.name, r.declared)) {
                /* begin may have partially initialised resources. */
                if (r.sink->abort) r.sink->abort(r.sink->context);
                return fail(&r, YMODEM_SINK);
            }
            r.sink_open = 1;
            r.received = 0;
            r.blocks = 0;
            r.last_seq = 0;
            r.have_last = 1;
            if (stats) {
                copy_name(stats, r.name);
                stats->size = r.declared;
            }
            note_progress(&r);
            if (tx_byte(&r, SYM_ACK)) return fail(&r, YMODEM_IO);
            if (tx_byte(&r, SYM_CRC)) return fail(&r, YMODEM_IO);
            current = PHASE_DATA;
            continue;
        }
        if (current == PHASE_DATA) {
            if (r.have_last && f.seq == r.last_seq) {
                /* Retransmission after a lost ACK: acknowledge, write nothing.
                   Counting this as no progress makes a peer that only repeats
                   frames terminate deterministically. */
                int limit = no_progress_event(&r, 0);
                if (limit) return fail(&r, limit);
                if (tx_byte(&r, SYM_ACK)) return fail(&r, YMODEM_IO);
                if (r.received == 0 && tx_byte(&r, SYM_CRC))
                    return fail(&r, YMODEM_IO);
                continue;
            }
            uint8_t expected = (uint8_t)(r.last_seq + 1u);
            if (f.seq != expected) {
                int limit = no_progress_event(&r, 0);
                if (limit) return fail(&r, limit);
                if (send_request(&r, SYM_NAK)) return fail(&r, YMODEM_IO);
                continue;
            }
            if (r.received == r.declared) return fail(&r, YMODEM_OVERFLOW);
            uint32_t remaining = r.declared - r.received;
            uint32_t take = f.size < remaining ? f.size : remaining;
            if (take && r.sink->write(r.sink->context, f.data, take))
                return fail(&r, YMODEM_SINK);
            r.received += take;
            ++r.blocks;
            r.last_seq = f.seq;
            r.have_last = 1;
            note_progress(&r);
            if (stats) stats->received = r.received;
            if (tx_byte(&r, SYM_ACK)) return fail(&r, YMODEM_IO);
            continue;
        }
        /* PHASE_FINAL: only the empty terminating block 0 is acceptable. */
        if (f.seq != 0) return fail(&r, YMODEM_PROTOCOL);
        if (f.data[0] != 0) return fail(&r, YMODEM_MULTI_FILE);
        if (r.sink->finish(r.sink->context, r.received))
            return fail(&r, YMODEM_SINK);
        r.sink_open = 0;
        /* finish() returned 0: the file is committed. The remaining protocol
           bytes are best effort and must not turn success into failure. */
        (void)tx_byte(&r, SYM_ACK);
        if (stats) stats->blocks = r.blocks;
        /* A lost terminator ACK can make the sender repeat the empty block 0.
           Acknowledge bounded repeats; finish() is never called again. */
        uint32_t quiet = 0;
        while (quiet <= r.config->retry_limit) {
            int again = read_frame(&r, &f);
            if (again == RX_FRAME) {
                if (f.seq == 0 && f.data[0] == 0) {
                    (void)tx_byte(&r, SYM_ACK);
                    quiet = 0;
                    continue;
                }
                break; /* data or a second file: the session is already over */
            }
            if (again == RX_IO || again == RX_CANCEL) break;
            ++quiet; /* timeout, idle budget, bad frame or repeated EOT */
        }
        return YMODEM_OK;
    }
}
