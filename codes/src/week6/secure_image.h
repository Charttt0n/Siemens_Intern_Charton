/** @file
 * @brief Signed image verification with the compiled-in lab public key.
 */
#ifndef WEEK6_SECURE_IMAGE_H
#define WEEK6_SECURE_IMAGE_H
#include "image.h"

/** Week-6 signature trailer placed immediately after the Week-5 image.
 * Type 1 is RSA-2048 + SHA-256 + PKCS#1 v1.5 over SHA256(header||payload). */
#define SIG_MAGIC 0x4753364du /**< little-endian "M6SG" */
#define SIG_TYPE_RSA2048_SHA256_PKCS1V15 1u
#define SIG_LENGTH 256u
#define SIG_TRAILER_SIZE 272u
#define SECURE_MAX_PAYLOAD (APP_EXEC_SIZE - IMAGE_HEADER_SIZE - SIG_TRAILER_SIZE)

typedef enum {
    SECURE_OK = 0,
    SECURE_IO = 1,
    SECURE_IMAGE = 2,
    SECURE_TRAILER = 10,
    SECURE_UNSIGNED = 11,
    SECURE_TYPE = 12,
    SECURE_HASH = 13,
    SECURE_SIGNATURE = 14,
    SECURE_KEY = 15,
    SECURE_INIT = 16
} secure_result;

/** One-time Mbed TLS buffer-allocator setup. Safe to call more than once. */
int secure_init(void);

/** Week-5 slot checks, then require a valid trailer and signature.
 * On success, info matches image_validate_slot(). */
int secure_validate_slot(const flash_device *dev, flash_partition_id slot,
                         image_info *info);
#endif
