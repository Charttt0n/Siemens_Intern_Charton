/* Regression for the review reproduction of 2026-10-30.
 *
 * The original /tmp reproduction replayed one data frame forever and only
 * stopped when IO was injected. It also used a positive write return, which
 * the documented ymodem_io contract now rejects; this copy keeps the exact
 * frame replay but uses the contract (write returns 0) and the extended
 * config initializer. Expected behaviour: bounded no-progress retries finish
 * with YMODEM_RETRY_LIMIT long before any read cap.
 */
#include "ymodem.h"
#include "crc16.h"
#include <stdio.h>
#include <string.h>

static unsigned reads, pos, writes, begins, finishes, sinkwrites;
static unsigned char buf[266];

static void frame(unsigned off, unsigned seq, int header) {
    unsigned char *p = buf + off;
    p[0] = 1;
    p[1] = (unsigned char)seq;
    p[2] = (unsigned char)(255 - seq);
    memset(p + 3, 0, 128);
    if (header) memcpy(p + 3, "x\0" "1", 3);
    else p[3] = 'z';
    unsigned c = crc16(p + 3, 128);
    p[131] = (unsigned char)(c >> 8);
    p[132] = (unsigned char)c;
}

static int rd(void *c, uint8_t *b, uint32_t n) {
    (void)c; (void)n;
    if (++reads > 20000) return -1;
    *b = buf[pos++];
    if (pos == 266) pos = 133;
    return 1;
}

static int wr(void *c, const uint8_t *b, uint32_t n) {
    (void)c; (void)b; (void)n;
    ++writes;
    return 0;
}

static uint32_t tm(void *c) {
    (void)c;
    return reads;
}

static int begin(void *c, const char *n, uint32_t z) {
    (void)c; (void)n; (void)z;
    ++begins;
    return 0;
}

static int write_sink(void *c, const uint8_t *b, uint32_t n) {
    (void)c; (void)b; (void)n;
    ++sinkwrites;
    return 0;
}

static int finish(void *c, uint32_t n) {
    (void)c; (void)n;
    ++finishes;
    return 0;
}

int main(void) {
    frame(0, 0, 1);
    frame(133, 1, 0);
    ymodem_io io = {0, rd, wr, tm};
    ymodem_sink sink = {0, begin, write_sink, finish, 0};
    ymodem_config cfg = {1024, 10, 3, 0};
    int r = ymodem_receive(&io, &sink, &cfg, 0);
    printf("result=%d reads=%u sink_writes=%u (retry budget=3)\n",
           r, reads, sinkwrites);
    return (r == YMODEM_RETRY_LIMIT && reads < 2000 && sinkwrites == 1) ? 0 : 1;
}
