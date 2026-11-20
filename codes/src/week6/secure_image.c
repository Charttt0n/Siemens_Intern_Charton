#include "secure_image.h"
#include "crc32.h"
#include "week6_lab_public_key.h"
#include "watchdog.h"
#include "mbedtls/sha256.h"
#include "mbedtls/rsa.h"
#include "mbedtls/md.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/platform.h"

static uint8_t crypto_heap[24 * 1024];
static int ready;

int secure_init(void) {
    if (ready) return SECURE_OK;
    mbedtls_memory_buffer_alloc_init(crypto_heap, sizeof(crypto_heap));
    ready = 1;
    return SECURE_OK;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int hash_slot(const flash_device *dev, flash_partition_id slot,
                     uint32_t payload_size, unsigned char digest[32]) {
    uint8_t block[256];
    mbedtls_sha256_context ctx;
    uint32_t total = IMAGE_HEADER_SIZE + payload_size, offset = 0;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return SECURE_HASH;
    }
    while (offset < total) {
        uint32_t count = total - offset;
        if (count > sizeof(block)) count = sizeof(block);
        if (flash_read(dev, slot, offset, block, count)) {
            mbedtls_sha256_free(&ctx);
            return SECURE_IO;
        }
        if (mbedtls_sha256_update(&ctx, block, count) != 0) {
            mbedtls_sha256_free(&ctx);
            return SECURE_HASH;
        }
        offset += count;
        watchdog_kick();
    }
    if (mbedtls_sha256_finish(&ctx, digest) != 0) {
        mbedtls_sha256_free(&ctx);
        return SECURE_HASH;
    }
    mbedtls_sha256_free(&ctx);
    return SECURE_OK;
}

static int verify_signature(const unsigned char digest[32],
                            const unsigned char signature[SIG_LENGTH]) {
    mbedtls_rsa_context rsa;
    int status;
    mbedtls_rsa_init(&rsa);
    if (mbedtls_rsa_import_raw(&rsa,
                               week6_test_rsa_n, week6_test_rsa_n_len,
                               0, 0, 0, 0, 0, 0,
                               week6_test_rsa_e, week6_test_rsa_e_len) != 0 ||
        mbedtls_rsa_complete(&rsa) != 0 ||
        mbedtls_rsa_check_pubkey(&rsa) != 0) {
        mbedtls_rsa_free(&rsa);
        return SECURE_KEY;
    }
    status = mbedtls_rsa_rsassa_pkcs1_v15_verify(&rsa, MBEDTLS_MD_SHA256,
                                                 32, digest, signature);
    mbedtls_rsa_free(&rsa);
    return status == 0 ? SECURE_OK : SECURE_SIGNATURE;
}

int secure_validate_slot(const flash_device *dev, flash_partition_id slot,
                         image_info *info) {
    uint8_t trailer[SIG_TRAILER_SIZE];
    unsigned char digest[32];
    int image, hashed, verified;
    if (!ready && secure_init() != SECURE_OK) return SECURE_INIT;
    if (!info) return SECURE_IO;
    image = image_validate_slot(dev, slot, info);
    if (image != IMAGE_OK) return image == IMAGE_IO ? SECURE_IO : SECURE_IMAGE;
    if (info->size > SECURE_MAX_PAYLOAD) return SECURE_TRAILER;
    if (flash_read(dev, slot, IMAGE_HEADER_SIZE + info->size,
                   trailer, sizeof(trailer)))
        return SECURE_IO;
    if (le32(trailer) != SIG_MAGIC) return SECURE_UNSIGNED;
    if (le32(trailer + 4) != SIG_TYPE_RSA2048_SHA256_PKCS1V15 ||
        le32(trailer + 8) != SIG_LENGTH ||
        le32(trailer + 12) != 0)
        return SECURE_TYPE;
    hashed = hash_slot(dev, slot, info->size, digest);
    if (hashed != SECURE_OK) return hashed;
    verified = verify_signature(digest, trailer + 16);
    watchdog_kick();
    return verified;
}
