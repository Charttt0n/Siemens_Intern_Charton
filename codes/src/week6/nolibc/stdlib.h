#ifndef WEEK6_NOLIBC_STDLIB_H
#define WEEK6_NOLIBC_STDLIB_H
#include <stddef.h>
void abort(void);
void exit(int status);
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void free(void *p);
#endif
