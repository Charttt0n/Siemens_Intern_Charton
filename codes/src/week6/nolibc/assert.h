#ifndef WEEK6_NOLIBC_ASSERT_H
#define WEEK6_NOLIBC_ASSERT_H
#include <stdlib.h>
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) do { if (!(expr)) abort(); } while (0)
#endif
#endif
