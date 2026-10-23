#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H
#include "flash.h"
#define IMAGE_MAGIC 0x4d49374du /* little-endian "M7IM" */
#define IMAGE_FORMAT 1u
#define IMAGE_HEADER_SIZE 32u
#define APP_EXEC_BASE 0x00010000u
#define APP_EXEC_SIZE 0x70000u
#define APP_RAM_BASE 0x20010000u
#define APP_RAM_TOP 0x20200000u
#define APP_VECTOR_SIZE 192u
typedef enum {
    IMAGE_OK = 0, IMAGE_IO = 1, IMAGE_FORMAT_ERROR = 2, IMAGE_HEADER_CRC = 3,
    IMAGE_LENGTH = 4, IMAGE_ADDRESS = 5, IMAGE_PAYLOAD_CRC = 6,
    IMAGE_VECTOR = 7, IMAGE_COPY_CRC = 8
} image_result;
typedef struct { uint32_t size, load, version, crc, msp, reset; } image_info;
int image_validate(const flash_device *dev, image_info *info);
int image_load(const flash_device *dev, const image_info *info);
int boot_verify_loaded(const image_info *info);
#endif
