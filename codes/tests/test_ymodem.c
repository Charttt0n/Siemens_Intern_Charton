/* Host tests for the Week-5 YMODEM receiver (step 4).
 *
 * The sender side is scripted byte streams plus injected gaps; the receiver
 * under test is always the production ymodem_receive(). Real interoperability
 * with an independent third-party sender is checked by verify-week5.py. */
#include "ymodem.h"
#include "crc16.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

#define STREAM_MAX 80000u
#define SINK_MAX 80000u

typedef struct {
    uint8_t stream[STREAM_MAX];
    uint32_t stream_len;
    uint32_t stream_pos;
    uint8_t tx[8192];
    uint32_t tx_len;
    uint32_t now;
    uint32_t read_step_ms;
    uint32_t advance_per_read_ms; /* move the injected clock on every read */
    uint32_t loop_start;
    int loop;                    /* replay from loop_start at end of stream */
    int write_count_success;     /* return size instead of 0 (contract test) */
    int read_illegal_count;      /* return 2 from read (contract test) */
    uint32_t read_cap;           /* fail after this many reads (0 = none) */
    uint32_t read_fail_once_pos; int read_fail_once_done;
    uint32_t read_illegal_once_pos; int read_illegal_once_done;
    long read_fail_at;   /* 1-based read call index */
    uint32_t read_fail_pos; /* fail once the stream reaches this position */
    uint32_t pause_pos;
    uint32_t pause_ms;
    uint32_t pause_elapsed;
    long write_fail_at;  /* 1-based write call index */
    unsigned reads;
    unsigned writes;
} script;

typedef struct {
    uint8_t data[SINK_MAX];
    uint32_t len;
    char name[YMODEM_NAME_MAX];
    uint32_t declared;
    int begin_fail;
    long write_fail_at; /* 1-based sink write call */
    int finish_fail;
    unsigned begins, writes, finishes, aborts;
} sink_state;

typedef struct {
    script tx;
    sink_state sink;
    uint32_t no_progress_timeout;
} fixture;

static void fixture_init(fixture *f) {
    memset(f, 0, sizeof(*f));
    f->tx.read_step_ms = 10;
}

static int s_read(void *context, uint8_t *buffer, uint32_t size) {
    script *s = context;
    ++s->reads;
    if (s->advance_per_read_ms) s->now += s->advance_per_read_ms;
    if (s->read_cap && s->reads > s->read_cap) return -1;
    if (s->read_fail_once_pos && !s->read_fail_once_done &&
        s->stream_pos == s->read_fail_once_pos) {
        s->read_fail_once_done = 1;
        return -1;
    }
    if (s->read_illegal_once_pos && !s->read_illegal_once_done &&
        s->stream_pos == s->read_illegal_once_pos) {
        s->read_illegal_once_done = 1;
        return 2;
    }
    if (s->read_fail_at && (long)s->reads == s->read_fail_at) return -1;
    if (s->read_fail_pos && s->stream_pos >= s->read_fail_pos) return -1;
    if (s->read_illegal_count) return 2;
    if (s->pause_pos && s->stream_pos == s->pause_pos &&
        s->pause_elapsed < s->pause_ms) {
        s->pause_elapsed += s->read_step_ms;
        s->now += s->read_step_ms;
        return 0;
    }
    if (s->loop && s->stream_pos >= s->stream_len) s->stream_pos = s->loop_start;
    if (size == 0) return 0;
    if (s->stream_pos < s->stream_len) {
        buffer[0] = s->stream[s->stream_pos++];
        return 1;
    }
    s->now += s->read_step_ms;
    return 0;
}

static int s_write(void *context, const uint8_t *data, uint32_t size) {
    script *s = context;
    ++s->writes;
    if (s->write_fail_at && (long)s->writes == s->write_fail_at) return -1;
    if (s->write_count_success) return (int)size;
    if (s->tx_len + size > sizeof(s->tx)) return -1;
    memcpy(s->tx + s->tx_len, data, size);
    s->tx_len += size;
    return 0;
}

static uint32_t s_now(void *context) {
    return ((script *)context)->now;
}

static int k_begin(void *context, const char *name, uint32_t size) {
    sink_state *k = context;
    ++k->begins;
    if (k->begin_fail) return -1;
    size_t i = 0;
    while (i + 1 < YMODEM_NAME_MAX && name[i]) { k->name[i] = name[i]; ++i; }
    k->name[i] = 0;
    k->declared = size;
    return 0;
}

static int k_write(void *context, const uint8_t *data, uint32_t size) {
    sink_state *k = context;
    ++k->writes;
    if (k->write_fail_at && (long)k->writes == k->write_fail_at) return -1;
    if (k->len + size > SINK_MAX) return -1;
    memcpy(k->data + k->len, data, size);
    k->len += size;
    return 0;
}

static int k_finish(void *context, uint32_t size) {
    sink_state *k = context;
    ++k->finishes;
    k->declared = size;
    return k->finish_fail ? -1 : 0;
}

static void k_abort(void *context) {
    ++((sink_state *)context)->aborts;
}

static int run(fixture *f, uint32_t max_size, uint32_t timeout, uint32_t retries,
               ymodem_stats *stats) {
    ymodem_io io;
    ymodem_sink sink;
    ymodem_config config;
    io.context = &f->tx;
    io.read = s_read;
    io.write = s_write;
    io.now_ms = s_now;
    sink.context = &f->sink;
    sink.begin = k_begin;
    sink.write = k_write;
    sink.finish = k_finish;
    sink.abort = k_abort;
    config.max_file_size = max_size;
    config.timeout_ms = timeout;
    config.retry_limit = retries;
    config.no_progress_timeout_ms = f->no_progress_timeout;
    return ymodem_receive(&io, &sink, &config, stats);
}

static void put_frame(script *s, uint8_t seq, const uint8_t *data,
                      uint32_t size, int corrupt) {
    uint8_t *p = s->stream + s->stream_len;
    p[0] = size == 128u ? 0x01u : 0x02u;
    p[1] = seq;
    p[2] = (uint8_t)(0xFFu - seq);
    memcpy(p + 3, data, size);
    uint16_t crc = crc16(data, size);
    if (corrupt) crc ^= 0x0101u;
    p[3 + size] = (uint8_t)(crc >> 8);
    p[4 + size] = (uint8_t)crc;
    s->stream_len += 5u + size;
}

static void put_eot(script *s) {
    s->stream[s->stream_len++] = 0x04u;
}

static void put_cancel(script *s) {
    s->stream[s->stream_len++] = 0x18u;
    s->stream[s->stream_len++] = 0x18u;
}

static void put_block0(script *s, const char *name, const char *length,
                       uint32_t frame_size, int corrupt) {
    uint8_t data[1024];
    memset(data, 0, frame_size);
    size_t n = strlen(name);
    memcpy(data, name, n);
    size_t l = strlen(length);
    memcpy(data + n + 1, length, l);
    put_frame(s, 0, data, frame_size, corrupt);
}

/* Append payload in fixed-size frames; returns the next sequence number. */
static uint8_t put_data(script *s, const uint8_t *payload, uint32_t size,
                        uint32_t block_size, uint8_t seq) {
    uint8_t data[1024];
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t chunk = size - offset < block_size ? size - offset : block_size;
        memset(data, 0x1Au, block_size);
        memcpy(data, payload + offset, chunk);
        put_frame(s, seq, data, block_size, 0);
        seq = (uint8_t)(seq + 1u);
        offset += chunk;
    }
    return seq;
}

static void fill_payload(uint8_t *data, uint32_t size, uint32_t seed) {
    for (uint32_t i = 0; i < size; ++i)
        data[i] = (uint8_t)((i * 31u + seed) & 0xFFu);
}

static int test_crc16_known_vectors(void) {
    /* Cross-checked against the independent third-party ymodem.CRC.calc_crc16. */
    CHECK(crc16("123456789", 9) == 0x31C3u);
    CHECK(crc16("", 0) == 0x0000u);
    CHECK(crc16("A", 1) == 0x58E5u);
    uint8_t zeros[128];
    memset(zeros, 0, sizeof(zeros));
    CHECK(crc16(zeros, 128) == 0x0000u);
    uint8_t sequence[1024];
    for (uint32_t i = 0; i < sizeof(sequence); ++i) sequence[i] = (uint8_t)i;
    CHECK(crc16(sequence, 1024) == 0xC2E0u);
    uint8_t ones[128];
    memset(ones, 0xFF, sizeof(ones));
    CHECK(crc16(ones, 128) == 0xEDA9u);
    return 0;
}

static int test_success_mixed_frames(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[3000];
    fill_payload(payload, sizeof(payload), 1);
    put_block0(&f.tx, "firmware.bin", "3000", 128, 0);
    put_data(&f.tx, payload, 1024, 1024, 1);
    put_data(&f.tx, payload + 1024, 128, 128, 2);
    put_data(&f.tx, payload + 1152, 1024, 1024, 3);
    put_data(&f.tx, payload + 2176, 824, 1024, 4);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 40000, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == sizeof(payload));
    CHECK(memcmp(f.sink.data, payload, sizeof(payload)) == 0);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.aborts == 0);
    CHECK(strcmp(f.sink.name, "firmware.bin") == 0);
    CHECK(stats.size == 3000 && stats.received == 3000 && stats.blocks == 4);
    CHECK(stats.retries == 0 && strcmp(stats.name, "firmware.bin") == 0);
    CHECK(f.tx.tx_len == 10 && f.tx.tx[0] == 0x43u);
    for (uint32_t i = 0; i < f.tx.tx_len; ++i) {
        uint8_t c = f.tx.tx[i];
        CHECK(c == 0x43u || c == 0x06u || c == 0x15u || c == 0x18u);
    }
    return 0;
}

static int test_success_128_wrap(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[260 * 128];
    fill_payload(payload, sizeof(payload), 2);
    put_block0(&f.tx, "wrap.bin", "33280", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 1024, 0);
    ymodem_stats stats;
    CHECK(run(&f, 40000, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == sizeof(payload));
    CHECK(memcmp(f.sink.data, payload, sizeof(payload)) == 0);
    CHECK(stats.blocks == 260 && stats.received == 33280);
    CHECK(f.sink.finishes == 1 && f.sink.aborts == 0);
    return 0;
}

static int block0_case(const char *name, const char *length, uint32_t frame,
                       int expected) {
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, name, length, frame, 0);
    put_cancel(&f.tx);
    ymodem_stats stats;
    int result = run(&f, 4096, 100, 3, &stats);
    CHECK(result == expected);
    if (expected == YMODEM_PROTOCOL || expected == YMODEM_TOO_LARGE) {
        CHECK(f.sink.begins == 0);
        CHECK(f.tx.tx_len >= 3);
        CHECK(f.tx.tx[f.tx.tx_len - 2] == 0x18u &&
              f.tx.tx[f.tx.tx_len - 1] == 0x18u);
    }
    return 0;
}

static int test_block0_validation(void) {
    CHECK(block0_case("a.bin", "abc", 128, YMODEM_PROTOCOL) == 0);
    CHECK(block0_case("a.bin", "", 128, YMODEM_PROTOCOL) == 0);
    CHECK(block0_case("a.bin", "100x", 128, YMODEM_PROTOCOL) == 0);
    CHECK(block0_case("a/b.bin", "100", 128, YMODEM_PROTOCOL) == 0);
    CHECK(block0_case("a.bin", "4294967296", 128, YMODEM_TOO_LARGE) == 0);
    CHECK(block0_case("a.bin", "4097", 128, YMODEM_TOO_LARGE) == 0);
    CHECK(block0_case("a.bin", "00000000001", 128, YMODEM_TOO_LARGE) == 0);
    CHECK(block0_case("a.bin", "1 1700000000 0", 128, YMODEM_CANCELLED) == 0);
    CHECK(block0_case("a.bin", "0", 128, YMODEM_CANCELLED) == 0);
    char long_name[70];
    memset(long_name, 'n', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    CHECK(block0_case(long_name, "100", 128, YMODEM_PROTOCOL) == 0);
    char max_name[64];
    memset(max_name, 'm', sizeof(max_name) - 1);
    max_name[sizeof(max_name) - 1] = 0;
    CHECK(block0_case(max_name, "0", 128, YMODEM_CANCELLED) == 0);
    /* Names without a NUL terminator inside the frame are rejected. */
    fixture f;
    fixture_init(&f);
    uint8_t data[128];
    memset(data, 'a', sizeof(data));
    put_frame(&f.tx, 0, data, sizeof(data), 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_PROTOCOL);
    CHECK(f.sink.begins == 0);
    /* An empty block 0 terminates an empty batch without opening the sink. */
    fixture_init(&f);
    put_block0(&f.tx, "", "", 128, 0);
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 0 && f.sink.finishes == 0);
    return 0;
}

static int test_zero_length_file(void) {
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "empty.bin", "0", 128, 0);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.len == 0);
    CHECK(stats.size == 0 && stats.received == 0 && stats.blocks == 0);
    return 0;
}

static int test_last_block_padding(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[100];
    fill_payload(payload, sizeof(payload), 3);
    put_block0(&f.tx, "short.bin", "100", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == 100);
    CHECK(memcmp(f.sink.data, payload, 100) == 0);
    return 0;
}

static int test_bad_crc_retransmit(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 4);
    put_block0(&f.tx, "crc.bin", "128", 128, 0);
    uint8_t data[128];
    memset(data, 0x1A, sizeof(data));
    memcpy(data, payload, sizeof(payload));
    put_frame(&f.tx, 1, data, 128, 1); /* corrupt CRC */
    put_frame(&f.tx, 1, data, 128, 0); /* retransmission */
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == 128 && memcmp(f.sink.data, payload, 128) == 0);
    CHECK(stats.retries == 1);
    CHECK(f.tx.tx_len >= 1);
    int found_nak = 0;
    for (uint32_t i = 0; i < f.tx.tx_len; ++i)
        if (f.tx.tx[i] == 0x15u) found_nak = 1;
    CHECK(found_nak);
    /* A corrupted block 0 is retransmitted the same way. */
    fixture_init(&f);
    put_block0(&f.tx, "crc0.bin", "128", 128, 1);
    put_block0(&f.tx, "crc0.bin", "128", 128, 0);
    put_data(&f.tx, payload, 128, 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.len == 128);
    return 0;
}

static int test_wrong_sequence_complement(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 5);
    put_block0(&f.tx, "seq.bin", "128", 128, 0);
    /* Header with a deliberately wrong complement byte. */
    uint8_t *p = f.tx.stream + f.tx.stream_len;
    uint8_t data[128];
    memset(data, 0x1A, sizeof(data));
    memcpy(data, payload, sizeof(payload));
    p[0] = 0x01u;
    p[1] = 1;
    p[2] = 0xFDu; /* 0xFF - 1 is 0xFE, so the complement is wrong */
    memcpy(p + 3, data, sizeof(data));
    uint16_t crc = crc16(data, sizeof(data));
    p[3 + 128] = (uint8_t)(crc >> 8);
    p[4 + 128] = (uint8_t)crc;
    f.tx.stream_len += 5 + 128;
    put_frame(&f.tx, 1, data, 128, 0);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == 128 && stats.retries == 1);
    return 0;
}

static int test_duplicates(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[256];
    fill_payload(payload, sizeof(payload), 6);
    put_block0(&f.tx, "dup.bin", "256", 128, 0);
    put_block0(&f.tx, "dup.bin", "256", 128, 0); /* lost ACK for block 0 */
    put_data(&f.tx, payload, 128, 128, 1);
    put_data(&f.tx, payload, 128, 128, 1); /* lost ACK for block 1 */
    put_data(&f.tx, payload + 128, 128, 128, 2);
    put_eot(&f.tx);
    put_eot(&f.tx); /* lost ACK for EOT */
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.aborts == 0);
    CHECK(f.sink.len == 256 && memcmp(f.sink.data, payload, 256) == 0);
    CHECK(stats.blocks == 2 && stats.received == 256 && stats.retries == 0);
    return 0;
}

static int test_truncated_and_junk(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 7);
    put_block0(&f.tx, "cut.bin", "128", 128, 0);
    uint8_t data[128];
    memset(data, 0x1A, sizeof(data));
    memcpy(data, payload, sizeof(payload));
    /* Noise, then a frame that is cut short after 10 payload bytes, then a
       gap long enough to time out, then the full retransmission. */
    f.tx.stream[f.tx.stream_len++] = 0x00u;
    f.tx.stream[f.tx.stream_len++] = 0x7Eu;
    f.tx.stream[f.tx.stream_len++] = 0x01u;
    f.tx.stream[f.tx.stream_len++] = 1;
    f.tx.stream[f.tx.stream_len++] = 0xFEu;
    for (uint32_t i = 0; i < 10; ++i) f.tx.stream[f.tx.stream_len++] = 0x1Au;
    f.tx.pause_pos = f.tx.stream_len;
    f.tx.pause_ms = 120;
    put_frame(&f.tx, 1, data, 128, 0);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == 128 && memcmp(f.sink.data, payload, 128) == 0);
    CHECK(stats.retries >= 1);
    return 0;
}

static int test_cancel(void) {
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "cancel.bin", "128", 128, 0);
    put_cancel(&f.tx);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_CANCELLED);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 0 && f.sink.aborts == 1);
    CHECK(f.sink.len == 0);
    fixture_init(&f);
    put_cancel(&f.tx);
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_CANCELLED);
    CHECK(f.sink.begins == 0);
    return 0;
}

static int test_timeout_uses_clock(void) {
    fixture f;
    fixture_init(&f);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_TIMEOUT);
    CHECK(f.sink.begins == 0);
    CHECK(f.tx.tx_len == 6); /* initial C + three retries + abort CAN CAN */
    for (uint32_t i = 0; i < 4; ++i) CHECK(f.tx.tx[i] == 0x43u);
    CHECK(f.tx.tx[4] == 0x18u && f.tx.tx[5] == 0x18u);
    CHECK(f.tx.now >= 100u); /* time advanced by the injected clock */
    CHECK(f.tx.reads >= 40); /* not a fixed spin count */
    return 0;
}

static int test_retry_limit_bad_frames(void) {
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "bad.bin", "128", 128, 0);
    uint8_t data[128];
    memset(data, 0x1A, sizeof(data));
    for (unsigned i = 0; i < 4; ++i) put_frame(&f.tx, 1, data, 128, 1);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_RETRY_LIMIT);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    CHECK(stats.retries == 3);
    CHECK(f.tx.tx[f.tx.tx_len - 2] == 0x18u &&
          f.tx.tx[f.tx.tx_len - 1] == 0x18u);
    return 0;
}

static int test_final_handshake_timeout(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 8);
    put_block0(&f.tx, "final.bin", "128", 128, 0);
    put_data(&f.tx, payload, 128, 128, 1);
    put_eot(&f.tx);
    /* No terminating block 0: the receiver must retry 'C' a bounded number
       of times and then fail without finishing the sink. */
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_TIMEOUT);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 0 && f.sink.aborts == 1);
    CHECK(f.tx.tx_len >= 6);
    return 0;
}

static int test_tx_failure(void) {
    fixture f;
    fixture_init(&f);
    f.tx.write_fail_at = 1;
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 0);
    fixture_init(&f);
    put_block0(&f.tx, "tx.bin", "128", 128, 0);
    f.tx.write_fail_at = 2; /* ACK for block 0 */
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    return 0;
}

static int test_sink_failure(void) {
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "sink.bin", "128", 128, 0);
    f.sink.begin_fail = 1;
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_SINK);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    CHECK(f.tx.tx[f.tx.tx_len - 2] == 0x18u);
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 9);
    put_block0(&f.tx, "sink.bin", "128", 128, 0);
    put_data(&f.tx, payload, 128, 128, 1);
    f.sink.write_fail_at = 1;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_SINK);
    CHECK(f.sink.aborts == 1 && f.sink.finishes == 0);
    fixture_init(&f);
    put_block0(&f.tx, "sink.bin", "0", 128, 0);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    f.sink.finish_fail = 1;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_SINK);
    CHECK(f.sink.finishes == 1 && f.sink.aborts == 1);
    return 0;
}

static int test_underflow_and_overflow(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[512];
    fill_payload(payload, sizeof(payload), 10);
    put_block0(&f.tx, "short.bin", "1000", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    put_eot(&f.tx);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_UNDERFLOW);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    fixture_init(&f);
    uint8_t big[1024];
    fill_payload(big, sizeof(big), 11);
    put_block0(&f.tx, "long.bin", "512", 128, 0);
    put_data(&f.tx, big, sizeof(big), 1024, 1);
    put_data(&f.tx, big, sizeof(big), 1024, 2);
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OVERFLOW);
    CHECK(f.sink.aborts == 1 && f.sink.len == 512);
    return 0;
}

static int test_duplicate_terminator(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 13);
    put_block0(&f.tx, "term.bin", "128", 128, 0);
    put_data(&f.tx, payload, 128, 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    put_block0(&f.tx, "", "", 128, 0); /* lost ACK for the terminator */
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.aborts == 0);
    CHECK(f.sink.len == 128);
    /* Initial C, block0 ACK+C, data ACK, EOT ACK+C, two terminator ACKs. */
    CHECK(f.tx.tx_len == 8);
    CHECK(f.tx.tx[f.tx.tx_len - 1] == 0x06u);
    return 0;
}

static int test_multi_file(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 12);
    put_block0(&f.tx, "one.bin", "128", 128, 0);
    put_data(&f.tx, payload, 128, 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "two.bin", "128", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_MULTI_FILE);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 0 && f.sink.aborts == 1);
    /* After the empty terminator the session ends; a second file is ignored. */
    fixture_init(&f);
    put_block0(&f.tx, "one.bin", "128", 128, 0);
    put_data(&f.tx, payload, 128, 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    put_block0(&f.tx, "two.bin", "128", 128, 0);
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.aborts == 0);
    return 0;
}

static int test_read_io_error(void) {
    fixture f;
    fixture_init(&f);
    f.tx.read_fail_at = 1;
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 0);
    fixture_init(&f);
    put_block0(&f.tx, "io.bin", "128", 128, 0);
    f.tx.read_fail_pos = f.tx.stream_len;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1);
    return 0;
}

static int test_argument_checks(void) {
    fixture f;
    fixture_init(&f);
    ymodem_stats stats;
    ymodem_io io;
    ymodem_sink sink;
    ymodem_config config;
    io.context = &f.tx;
    io.read = s_read;
    io.write = s_write;
    io.now_ms = s_now;
    sink.context = &f.sink;
    sink.begin = k_begin;
    sink.write = k_write;
    sink.finish = k_finish;
    sink.abort = k_abort;
    config.max_file_size = 4096;
    config.timeout_ms = 100;
    config.retry_limit = 3;
    config.no_progress_timeout_ms = 0;
    CHECK(ymodem_receive(0, &sink, &config, &stats) == YMODEM_ARGUMENT);
    CHECK(ymodem_receive(&io, 0, &config, &stats) == YMODEM_ARGUMENT);
    CHECK(ymodem_receive(&io, &sink, 0, &stats) == YMODEM_ARGUMENT);
    ymodem_config bad = config;
    bad.max_file_size = 0;
    CHECK(ymodem_receive(&io, &sink, &bad, &stats) == YMODEM_ARGUMENT);
    bad = config;
    bad.timeout_ms = 0;
    CHECK(ymodem_receive(&io, &sink, &bad, &stats) == YMODEM_ARGUMENT);
    bad = config;
    bad.retry_limit = 0;
    CHECK(ymodem_receive(&io, &sink, &bad, &stats) == YMODEM_ARGUMENT);
    ymodem_io no_read = io;
    no_read.read = 0;
    CHECK(ymodem_receive(&no_read, &sink, &config, &stats) == YMODEM_ARGUMENT);
    ymodem_sink no_finish = sink;
    no_finish.finish = 0;
    CHECK(ymodem_receive(&io, &no_finish, &config, &stats) == YMODEM_ARGUMENT);
    return 0;
}


static int test_duplicate_bound(void) {
    /* Review reproduction: one data frame replayed forever. The old receiver
       reset its retry counter here and only stopped when IO was injected. */
    fixture f;
    fixture_init(&f);
    uint8_t payload[1] = {0x5A};
    put_block0(&f.tx, "dup.bin", "1", 128, 0);
    uint32_t data_start = f.tx.stream_len;
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    f.tx.loop = 1;
    f.tx.loop_start = data_start;
    f.tx.advance_per_read_ms = 1;
    f.tx.read_cap = 20000;
    ymodem_stats stats;
    CHECK(run(&f, 1024, 10, 3, &stats) == YMODEM_RETRY_LIMIT);
    CHECK(f.tx.reads < 2000);
    CHECK(f.sink.begins == 1 && f.sink.writes == 1 && f.sink.finishes == 0);
    CHECK(f.sink.aborts == 1);
    return 0;
}

static int test_no_progress_time_budget(void) {
    /* Bytes keep arriving, but no complete frame is accepted. The wall-clock
       budget must fire even though read() never returns 0. */
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "slow.bin", "128", 128, 0);
    uint32_t start = f.tx.stream_len;
    f.tx.stream[f.tx.stream_len++] = 0x01u;
    f.tx.stream[f.tx.stream_len++] = 1;
    f.tx.stream[f.tx.stream_len++] = 0xFEu;
    for (unsigned i = 0; i < 500; ++i) f.tx.stream[f.tx.stream_len++] = 0x1Au;
    f.tx.loop = 1;
    f.tx.loop_start = start;
    f.tx.advance_per_read_ms = 1;
    f.no_progress_timeout = 160;
    ymodem_stats stats;
    CHECK(run(&f, 1024, 10, 1000, &stats) == YMODEM_TIMEOUT);
    CHECK(f.tx.reads < 400);
    return 0;
}

static int test_noise_bound(void) {
    fixture f;
    fixture_init(&f);
    memset(f.tx.stream, 0x00, 512);
    f.tx.stream_len = 512;
    f.tx.loop = 1;
    f.tx.loop_start = 0;
    f.tx.advance_per_read_ms = 1;
    f.no_progress_timeout = 100000;
    ymodem_stats stats;
    CHECK(run(&f, 1024, 10, 3, &stats) == YMODEM_RETRY_LIMIT);
    CHECK(f.tx.reads < 30000);
    CHECK(f.sink.begins == 0);
    return 0;
}

static int test_progress_resets_budget(void) {
    /* A normal multi-block transfer must not trip the no-progress budget: each
       accepted block restarts it. */
    fixture f;
    fixture_init(&f);
    uint8_t payload[512];
    fill_payload(payload, sizeof(payload), 22);
    put_block0(&f.tx, "prog.bin", "512", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    f.tx.advance_per_read_ms = 1;
    f.no_progress_timeout = 200;
    ymodem_stats stats;
    CHECK(run(&f, 1024, 10, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.len == sizeof(payload));
    CHECK(memcmp(f.sink.data, payload, sizeof(payload)) == 0);
    return 0;
}

static int test_repeated_eot_bound(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 23);
    put_block0(&f.tx, "eot.bin", "128", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    uint32_t eot_start = f.tx.stream_len;
    put_eot(&f.tx);
    f.tx.loop = 1;
    f.tx.loop_start = eot_start;
    f.tx.advance_per_read_ms = 1;
    f.no_progress_timeout = 100000;
    ymodem_stats stats;
    CHECK(run(&f, 1024, 10, 3, &stats) == YMODEM_RETRY_LIMIT);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 0 && f.sink.aborts == 1);
    CHECK(f.tx.reads < 5000);
    return 0;
}

static int test_delayed_terminator(void) {
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 24);
    put_block0(&f.tx, "term.bin", "128", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    /* Lost terminator ACK: the sender repeats the empty block only after a
       delay longer than one byte timeout. finish() must still be called once. */
    f.tx.pause_pos = f.tx.stream_len;
    f.tx.pause_ms = 250;
    put_block0(&f.tx, "", "", 128, 0);
    ymodem_stats stats;
    CHECK(run(&f, 1024, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.aborts == 0);
    CHECK(f.sink.len == sizeof(payload));
    CHECK(f.tx.tx_len == 8 && f.tx.tx[f.tx.tx_len - 1] == 0x06u);
    return 0;
}

static int test_finish_committed_ack_failure(void) {
    /* finish() is the commit point: a failed terminator ACK after that must
       not turn success into failure or call abort again. */
    fixture f;
    fixture_init(&f);
    uint8_t payload[128];
    fill_payload(payload, sizeof(payload), 25);
    put_block0(&f.tx, "commit.bin", "128", 128, 0);
    put_data(&f.tx, payload, sizeof(payload), 128, 1);
    put_eot(&f.tx);
    put_block0(&f.tx, "", "", 128, 0);
    f.tx.write_fail_at = 7; /* terminator ACK */
    ymodem_stats stats;
    CHECK(run(&f, 1024, 100, 3, &stats) == YMODEM_OK);
    CHECK(f.sink.begins == 1 && f.sink.finishes == 1 && f.sink.aborts == 0);
    CHECK(f.sink.len == sizeof(payload));
    return 0;
}

static int test_strict_io_contracts(void) {
    /* A positive write return is not the documented 0=complete contract. */
    fixture f;
    fixture_init(&f);
    f.tx.write_count_success = 1;
    ymodem_stats stats;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 0);
    /* read may only return 0/1 for the one-byte requests. */
    fixture_init(&f);
    f.tx.read_illegal_count = 1;
    CHECK(run(&f, 4096, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 0);
    return 0;
}


/* Append a frame whose sequence complement is wrong, including its payload and
   CRC tail, and return the stream offset where the frame starts. */
static uint32_t put_bad_inverse(script *s, uint8_t seq, uint32_t frame_size) {
    uint32_t start = s->stream_len;
    uint8_t *p = s->stream + s->stream_len;
    p[0] = frame_size == 128u ? 0x01u : 0x02u;
    p[1] = seq;
    p[2] = (uint8_t)(0xFEu - seq); /* 0xFF - seq is the correct complement */
    for (uint32_t i = 0; i < frame_size; ++i) p[3 + i] = 0x1Au;
    p[3 + frame_size] = 0;
    p[4 + frame_size] = 0;
    s->stream_len += 5u + frame_size;
    return start;
}

static int test_bad_frame_discard_io(void) {
    /* A one-shot negative read while draining a bad sequence pair must be
       reported as IO, not swallowed. */
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "discard.bin", "128", 128, 0);
    uint32_t bad = put_bad_inverse(&f.tx, 1, 128);
    f.tx.read_fail_once_pos = bad + 3 + 10; /* inside the payload drain */
    ymodem_stats stats;
    CHECK(run(&f, 1024, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    /* One-shot illegal positive count in the same place is also IO. */
    fixture_init(&f);
    put_block0(&f.tx, "discard.bin", "128", 128, 0);
    bad = put_bad_inverse(&f.tx, 1, 128);
    f.tx.read_illegal_once_pos = bad + 3 + 10;
    CHECK(run(&f, 1024, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    return 0;
}

static int test_bad_frame_discard_crc_io(void) {
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "discard.bin", "128", 128, 0);
    uint32_t bad = put_bad_inverse(&f.tx, 1, 128);
    f.tx.read_fail_once_pos = bad + 3 + 128; /* first CRC tail byte */
    ymodem_stats stats;
    CHECK(run(&f, 1024, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    fixture_init(&f);
    put_block0(&f.tx, "discard.bin", "128", 128, 0);
    bad = put_bad_inverse(&f.tx, 1, 128);
    f.tx.read_illegal_once_pos = bad + 3 + 128;
    CHECK(run(&f, 1024, 100, 3, &stats) == YMODEM_IO);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    return 0;
}

static int test_bad_frame_discard_budget(void) {
    /* The discard of a 1K bad frame must also respect the wall-clock budget. */
    fixture f;
    fixture_init(&f);
    put_block0(&f.tx, "discard.bin", "128", 128, 0);
    put_bad_inverse(&f.tx, 1, 1024);
    f.tx.advance_per_read_ms = 1;
    f.no_progress_timeout = 140; /* block0 is 133 bytes, then the 1K drain */
    ymodem_stats stats;
    CHECK(run(&f, 1024, 10, 1000, &stats) == YMODEM_TIMEOUT);
    CHECK(f.sink.begins == 1 && f.sink.aborts == 1 && f.sink.finishes == 0);
    return 0;
}

int main(void) {
    static const struct { const char *name; int (*run_test)(void); } tests[] = {
        {"crc16-known-vectors", test_crc16_known_vectors},
        {"success-mixed-frames", test_success_mixed_frames},
        {"success-128-wrap", test_success_128_wrap},
        {"block0-validation", test_block0_validation},
        {"zero-length-file", test_zero_length_file},
        {"last-block-padding", test_last_block_padding},
        {"bad-crc-retransmit", test_bad_crc_retransmit},
        {"wrong-sequence-complement", test_wrong_sequence_complement},
        {"duplicates", test_duplicates},
        {"truncated-and-junk", test_truncated_and_junk},
        {"cancel", test_cancel},
        {"timeout-uses-clock", test_timeout_uses_clock},
        {"retry-limit-bad-frames", test_retry_limit_bad_frames},
        {"final-handshake-timeout", test_final_handshake_timeout},
        {"tx-failure", test_tx_failure},
        {"sink-failure", test_sink_failure},
        {"underflow-overflow", test_underflow_and_overflow},
        {"duplicate-terminator", test_duplicate_terminator},
        {"multi-file", test_multi_file},
        {"read-io-error", test_read_io_error},
        {"duplicate-bound", test_duplicate_bound},
        {"no-progress-time-budget", test_no_progress_time_budget},
        {"noise-bound", test_noise_bound},
        {"progress-resets-budget", test_progress_resets_budget},
        {"repeated-eot-bound", test_repeated_eot_bound},
        {"delayed-terminator", test_delayed_terminator},
        {"finish-committed-ack-failure", test_finish_committed_ack_failure},
        {"strict-io-contracts", test_strict_io_contracts},
        {"bad-frame-discard-io", test_bad_frame_discard_io},
        {"bad-frame-discard-crc-io", test_bad_frame_discard_crc_io},
        {"bad-frame-discard-budget", test_bad_frame_discard_budget},
        {"argument-checks", test_argument_checks},
    };
    for (unsigned i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
        if (tests[i].run_test()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
    }
    puts("PASS: ymodem frames, block0 bounds, CRC/retry, duplicates, cancel, timeouts, sink");
    return 0;
}
