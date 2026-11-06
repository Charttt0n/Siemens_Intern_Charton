#ifndef WEEK5_YMODEM_H
#define WEEK5_YMODEM_H
#include <stdint.h>

/* Single-file YMODEM batch receiver with 128 and 1024 byte frames.
 *
 * Protocol basis: the YMODEM batch protocol as documented by Chuck Forsberg
 * (the "YMODEM Protocol reference" distributed with the pinned host sender in
 * tests/third_party/ymodem, ymodem 1.5.3, MIT). This C receiver is an
 * independent implementation written for this project: no available C
 * receiver with a compatible licence and a hardware-independent byte/time
 * interface was found in the workspace, so the protocol behaviour is derived
 * from the reference and validated against the third-party host sender.
 *
 * Properties required by the Week-5 plan:
 *   - block 0 carries a NUL-terminated file name and a bounded decimal length;
 *   - 128 byte (SOH) and 1024 byte (STX) frames may be mixed in one transfer;
 *   - sequence and its complement, CRC-16/XMODEM per frame;
 *   - every no-progress path (timeouts, bad frames, noise, repeat block 0,
 *     repeat data frames, repeat EOT and the post-commit terminator wait) is
 *     bounded by one unified consecutive-event counter AND by an actual
 *     wall-clock budget measured with the injected monotonic clock. The
 *     budget covers waits where bytes keep arriving, not only silent gaps;
 *   - only accepting block 0 or new data bytes resets that budget; repeated
 *     EOT frames never refresh it;
 *   - duplicate frames caused by a lost ACK are acknowledged but never written
 *     to the sink twice; sequence 255 -> 0 is data, not a second block 0;
 *   - EOT is acknowledged, then the terminating empty block 0 is required;
 *   - only the declared length is handed to the sink; excess data, short data
 *     and a second file are rejected;
 *   - cancellation (two CAN bytes) aborts without finishing the sink.
 *
 * The byte channel carries protocol characters only ('C' = 0x43, ACK 0x06,
 * NAK 0x15, CAN 0x18, EOT 0x04). This module never formats text or logs, so
 * callers can keep diagnostics on a separate channel. */
#define YMODEM_NAME_MAX 64u
#define YMODEM_PACKET_MAX 1024u

typedef enum {
    YMODEM_OK = 0,
    YMODEM_IO = 1,           /* read or write callback failed / bad return */
    YMODEM_TIMEOUT = 2,      /* no-progress wall-clock budget exhausted */
    YMODEM_CANCELLED = 3,    /* sender sent the CAN CAN abort sequence */
    YMODEM_PROTOCOL = 4,     /* malformed header, name or length field */
    YMODEM_RETRY_LIMIT = 5,  /* consecutive no-progress events exceeded limit */
    YMODEM_SINK = 6,         /* begin/write/finish reported a failure */
    YMODEM_TOO_LARGE = 7,    /* declared length exceeds max_file_size */
    YMODEM_UNDERFLOW = 8,    /* EOT before all declared bytes arrived */
    YMODEM_OVERFLOW = 9,     /* data blocks after the declared length */
    YMODEM_MULTI_FILE = 10,  /* terminating block 0 named another file */
    YMODEM_ARGUMENT = 11     /* invalid configuration or callbacks */
} ymodem_result;

typedef struct {
    void *context;
    /* Binary byte channel. read returns 1..size bytes actually read, 0 when no
       byte is available right now, or a negative value on failure. The
       receiver only ever asks for one byte and treats any other count as an
       I/O error.
       write returns exactly 0 only after the complete buffer has been
       written, or a negative value on failure. The adapter owns short-write
       and EAGAIN handling with a bounded wait; it must never return a positive
       byte count. The receiver treats every non-zero return, including a
       positive one, as YMODEM_IO. */
    int (*read)(void *context, uint8_t *buffer, uint32_t size);
    int (*write)(void *context, const uint8_t *data, uint32_t size);
    /* Monotonic milliseconds. Must not wrap during a transfer in practice. */
    uint32_t (*now_ms)(void *context);
} ymodem_io;

typedef struct {
    void *context;
    /* Called once for a validated block 0. name is NUL-terminated.
       Return 0 when fully initialised, non-zero on failure. The receiver calls
       abort() after a failed begin, because begin may have partially created
       resources; abort must tolerate that. */
    int (*begin)(void *context, const char *name, uint32_t size);
    /* Returns 0 after all bytes were accepted, non-zero on failure. */
    int (*write)(void *context, const uint8_t *data, uint32_t size);
    /* Called once for the terminating empty block 0; this is the commit point.
       Returning 0 means the file is committed: the receiver then reports
       YMODEM_OK even if the final ACK cannot be written, never calls abort and
       never calls finish again. A non-zero return is a failure and abort is
       invoked. */
    int (*finish)(void *context, uint32_t size);
    /* Optional cleanup after any failure, including a failed begin. Never
       called after a successful finish. Must be safe with no open file. */
    void (*abort)(void *context);
} ymodem_sink;

typedef struct {
    uint32_t max_file_size; /* largest accepted declared length */
    uint32_t timeout_ms;    /* per byte inactivity timeout */
    uint32_t retry_limit;   /* consecutive no-progress events before giving up */
    /* Wall-clock no-progress budget covering both silent gaps and continuous
       byte delivery. 0 derives timeout_ms * (YMODEM_PACKET_MAX + 8), i.e. one
       maximum-length frame, which is enough for a normal block and small
       enough that a stalling or repeating peer cannot run forever. Only
       accepting block 0 or new data bytes restarts it. For a large file this
       is per block, not per file. Set explicitly for a tighter bound. */
    uint32_t no_progress_timeout_ms;
} ymodem_config;

typedef struct {
    char name[YMODEM_NAME_MAX];
    uint32_t size;     /* declared length */
    uint32_t received; /* bytes handed to the sink */
    uint32_t blocks;   /* data frames accepted */
    uint32_t retries;  /* NAK / 'C' retries transmitted */
} ymodem_stats;

/* Receive exactly one file (plus the batch terminator). stats may be NULL. */
int ymodem_receive(const ymodem_io *io, const ymodem_sink *sink,
                   const ymodem_config *config, ymodem_stats *stats);
#endif
