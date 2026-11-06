/* Host harness for the Week-5 YMODEM receiver.
 *
 *   ymodem_host receive --output FILE [--max-size N] [--timeout MS]
 *                       [--retries N] [--no-progress-timeout MS]
 *       stdin/stdout are the binary byte channel; the received file goes to
 *       FILE and one JSON summary line goes to stderr. Nothing is printed to
 *       stdout, so the channel stays protocol-only. The write callback loops
 *       over short writes and returns 0 only when the whole buffer is out.
 *
 *   ymodem_host crc16 < input
 *       Prints the CRC-16/XMODEM of stdin as four hex digits, used to
 *       cross-check the C implementation against an independent one.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ymodem.h"
#include "crc16.h"

typedef struct {
    int input;
    int output;
} channel;

static int channel_read(void *context, uint8_t *buffer, uint32_t size) {
    channel *link = context;
    struct pollfd watched;
    watched.fd = link->input;
    watched.events = POLLIN;
    watched.revents = 0;
    int ready = poll(&watched, 1, 20);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    if (ready == 0) return 0;
    ssize_t count = read(link->input, buffer, size);
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN) return 0;
        return -1;
    }
    return (int)count;
}

static int channel_write(void *context, const uint8_t *data, uint32_t size) {
    channel *link = context;
    uint32_t done = 0;
    while (done < size) {
        ssize_t count = write(link->output, data + done, size - done);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (uint32_t)count;
    }
    return 0;
}

static uint32_t clock_ms(void *context) {
    (void)context;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u +
                      (uint64_t)now.tv_nsec / 1000000u);
}

typedef struct {
    const char *path;
    FILE *file;
    char name[YMODEM_NAME_MAX];
} file_sink;

static int sink_begin(void *context, const char *name, uint32_t size) {
    file_sink *sink = context;
    (void)size;
    size_t i = 0;
    while (i + 1 < YMODEM_NAME_MAX && name[i]) {
        sink->name[i] = name[i];
        ++i;
    }
    sink->name[i] = 0;
    sink->file = fopen(sink->path, "wb");
    return sink->file ? 0 : -1;
}

static int sink_write(void *context, const uint8_t *data, uint32_t size) {
    file_sink *sink = context;
    return fwrite(data, 1, size, sink->file) == size ? 0 : -1;
}

static int sink_finish(void *context, uint32_t size) {
    file_sink *sink = context;
    (void)size;
    int result = fflush(sink->file);
    if (fclose(sink->file)) result = -1;
    sink->file = 0;
    return result;
}

static void sink_abort(void *context) {
    file_sink *sink = context;
    if (sink->file) fclose(sink->file);
    sink->file = 0;
}

static const char *result_name(int code) {
    switch (code) {
    case YMODEM_OK: return "OK";
    case YMODEM_IO: return "IO";
    case YMODEM_TIMEOUT: return "TIMEOUT";
    case YMODEM_CANCELLED: return "CANCELLED";
    case YMODEM_PROTOCOL: return "PROTOCOL";
    case YMODEM_RETRY_LIMIT: return "RETRY_LIMIT";
    case YMODEM_SINK: return "SINK";
    case YMODEM_TOO_LARGE: return "TOO_LARGE";
    case YMODEM_UNDERFLOW: return "UNDERFLOW";
    case YMODEM_OVERFLOW: return "OVERFLOW";
    case YMODEM_MULTI_FILE: return "MULTI_FILE";
    case YMODEM_ARGUMENT: return "ARGUMENT";
    default: return "UNKNOWN";
    }
}

static void print_summary(int code, const ymodem_stats *stats) {
    fputs("{\"result\":\"", stderr);
    fputs(result_name(code), stderr);
    fprintf(stderr, "\",\"code\":%d,\"name\":\"", code);
    for (const char *p = stats->name; *p; ++p) {
        if (*p == '"' || *p == '\\') fputc('\\', stderr);
        fputc(*p, stderr);
    }
    fprintf(stderr, "\",\"size\":%u,\"received\":%u,\"blocks\":%u,"
                    "\"retries\":%u}\n",
            (unsigned)stats->size, (unsigned)stats->received,
            (unsigned)stats->blocks, (unsigned)stats->retries);
}

static int receive_main(int argc, char **argv) {
    const char *output = 0;
    uint32_t max_size = 1u << 20;
    uint32_t timeout = 1000;
    uint32_t retries = 10;
    uint32_t no_progress = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--output") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--max-size") && i + 1 < argc)
            max_size = (uint32_t)strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc)
            timeout = (uint32_t)strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--retries") && i + 1 < argc)
            retries = (uint32_t)strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--no-progress-timeout") && i + 1 < argc)
            no_progress = (uint32_t)strtoul(argv[++i], 0, 10);
        else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
    if (!output) {
        fputs("--output is required\n", stderr);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    channel link;
    link.input = 0;
    link.output = 1;
    ymodem_io io;
    io.context = &link;
    io.read = channel_read;
    io.write = channel_write;
    io.now_ms = clock_ms;
    file_sink sink;
    sink.path = output;
    sink.file = 0;
    sink.name[0] = 0;
    ymodem_sink target;
    target.context = &sink;
    target.begin = sink_begin;
    target.write = sink_write;
    target.finish = sink_finish;
    target.abort = sink_abort;
    ymodem_config config;
    config.max_file_size = max_size;
    config.timeout_ms = timeout;
    config.retry_limit = retries;
    config.no_progress_timeout_ms = no_progress;
    ymodem_stats stats;
    int result = ymodem_receive(&io, &target, &config, &stats);
    print_summary(result, &stats);
    return result == YMODEM_OK ? 0 : 1;
}

static int crc_main(void) {
    static uint8_t buffer[1u << 20];
    size_t total = 0;
    for (;;) {
        if (total == sizeof(buffer)) {
            fputs("input too large\n", stderr);
            return 2;
        }
        ssize_t count = read(0, buffer + total, sizeof(buffer) - total);
        if (count < 0) {
            if (errno == EINTR) continue;
            return 2;
        }
        if (count == 0) break;
        total += (size_t)count;
    }
    printf("%04x\n", (unsigned)crc16(buffer, (uint32_t)total));
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "receive")) return receive_main(argc, argv);
    if (argc == 2 && !strcmp(argv[1], "crc16")) return crc_main();
    fputs("usage: ymodem_host receive --output FILE [--max-size N] "
          "[--timeout MS] [--retries N] [--no-progress-timeout MS]\n"
          "       ymodem_host crc16 < input\n", stderr);
    return 2;
}
