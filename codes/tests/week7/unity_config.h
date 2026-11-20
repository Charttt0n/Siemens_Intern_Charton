#ifndef WEEK7_UNITY_CONFIG_H
#define WEEK7_UNITY_CONFIG_H
#define UNITY_EXCLUDE_MATH_H
#define UNITY_OUTPUT_FLUSH()
#define UNITY_EXCLUDE_FLOAT
#define UNITY_EXCLUDE_DOUBLE
#define UNITY_EXCLUDE_SETJMP_H
#define UNITY_OUTPUT_CHAR(c) unity_putchar(c)
void unity_putchar(int c);
#endif
