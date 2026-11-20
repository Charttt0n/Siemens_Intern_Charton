#ifndef WEEK6_NOLIBC_STDIO_H
#define WEEK6_NOLIBC_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct week6_file { int unused; } FILE;
extern FILE *stderr;
extern FILE *stdout;
int snprintf(char *s, size_t n, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
#endif
