/** @file
 * @brief CRC-16/XMODEM for serial data frames.
 */
#ifndef WEEK5_CRC16_H
#define WEEK5_CRC16_H
#include <stdint.h>
/** CRC-16/XMODEM: polynomial 0x1021, initial value 0x0000, no reflection and
   no final XOR. This is the CRC used by XMODEM-CRC/YMODEM data blocks. */
uint16_t crc16(const void *data, uint32_t size);
#endif
