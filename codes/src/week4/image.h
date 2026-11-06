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
    IMAGE_VECTOR = 7, IMAGE_COPY_CRC = 8, IMAGE_SLOT = 9
} image_result;
/* slot records the partition that produced this metadata. The Week-4 load
   entry point only accepts FLASH_APP; image_load_slot() requires info->slot to
   match the slot passed in, so metadata cannot be reused across slots. */
typedef struct { uint32_t size, load, version, crc, msp, reset;
                 flash_partition_id slot; } image_info;
/* Only FLASH_APP and FLASH_BACKUP may hold an application image. */
int image_slot_supported(flash_partition_id slot);
/* Week-4 entry points: pinned to FLASH_APP. */
int image_validate(const flash_device *dev, image_info *info);
int image_load(const flash_device *dev, const image_info *info);
/* Week-5 entry points: validate/load one explicitly named firmware slot.
   Execution address, vector rules and copy verification are slot-independent. */
int image_validate_slot(const flash_device *dev, flash_partition_id slot, image_info *info);
int image_load_slot(const flash_device *dev, flash_partition_id slot, const image_info *info);
/* Verifies the bytes copied to APP_EXEC_BASE; slot-independent by design. */
int boot_verify_loaded(const image_info *info);
#endif
