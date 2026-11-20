/** @file
 * @brief Millisecond clock used by the protocol receiver.
 */
#ifndef WEEK5_TIMEBASE_H
#define WEEK5_TIMEBASE_H
#include <stdint.h>
/** 1 ms monotonic SysTick counter used by the YMODEM receiver.
   Named timebase_* so src/week5/timebase.h cannot shadow the C library
   <time.h> through -Isrc/week5. */
/** Set up SysTick for a 1 ms period. */
void timebase_init(void);
/** Return the current unsigned millisecond tick count. */
uint32_t timebase_ms(void);
#endif
