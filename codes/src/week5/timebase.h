#ifndef WEEK5_TIMEBASE_H
#define WEEK5_TIMEBASE_H
#include <stdint.h>
/* 1 ms monotonic SysTick counter used by the YMODEM receiver.
   Named timebase_* so src/week5/timebase.h cannot shadow the C library
   <time.h> through -Isrc/week5. */
void timebase_init(void);
uint32_t timebase_ms(void);
#endif
