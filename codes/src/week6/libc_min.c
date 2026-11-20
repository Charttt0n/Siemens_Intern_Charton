/* Minimal C routines for -nostdlib -fno-builtin Week-6 targets. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d == s || n == 0) return dest;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

void *memset(void *dest, int value, size_t n) {
    uint8_t *d = dest;
    while (n--) *d++ = (uint8_t)value;
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        ++p; ++q;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { ++a; ++b; --n; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if ((unsigned char)*s == (unsigned char)c) return (char *)s;
        ++s;
    }
    return c == 0 ? (char *)s : 0;
}

FILE week6_stderr, week6_stdout;
FILE *stderr = &week6_stderr;
FILE *stdout = &week6_stdout;

void abort(void) { for (;;) __asm__ volatile ("wfi"); }
void exit(int status) { (void)status; abort(); }
void *malloc(size_t n) { (void)n; return 0; }
void *calloc(size_t n, size_t size) { (void)n; (void)size; return 0; }
void free(void *p) { (void)p; }

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) {
    (void)fmt; (void)ap;
    if (s && n) s[0] = 0;
    return 0;
}
int snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
int fprintf(FILE *stream, const char *fmt, ...) {
    (void)stream; (void)fmt;
    return 0;
}
