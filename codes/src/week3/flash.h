#ifndef WEEK3_FLASH_H
#define WEEK3_FLASH_H
#include <stdint.h>

#define FLASH_SIZE 0x100000u
#define FLASH_ERASE_SIZE 0x1000u
#define FLASH_PROGRAM_SIZE 4u
#define FLASH_SIM_BASE 0x20300000u
typedef enum { FLASH_BOOT, FLASH_APP, FLASH_BACKUP, FLASH_PARAMS,
               FLASH_PARTITION_COUNT } flash_partition_id;
typedef enum { FLASH_OK = 0, FLASH_ARGUMENT = -1, FLASH_RANGE = -2,
               FLASH_ALIGNMENT = -3, FLASH_PROTECTED = -4,
               FLASH_NEEDS_ERASE = -5 } flash_result;
typedef struct { uint32_t offset, size; } flash_partition;
typedef struct { volatile uint8_t *memory; } flash_device;

/* The backend is caller-owned SRAM. Binding never erases it. Only thread-mode,
   single-caller use is supported. This is a NOR-rule model, not an MCU driver. */
int flash_sim_bind(flash_device *dev, volatile uint8_t *memory, uint32_t size);
const flash_partition *flash_partition_get(flash_partition_id id);
int flash_read(const flash_device *dev, flash_partition_id id,
               uint32_t offset, void *output, uint32_t size);
int flash_program(flash_device *dev, flash_partition_id id,
                  uint32_t offset, const void *input, uint32_t size);
int flash_erase(flash_device *dev, flash_partition_id id,
                uint32_t offset, uint32_t size);
#endif
