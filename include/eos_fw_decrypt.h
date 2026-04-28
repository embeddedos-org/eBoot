// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_fw_decrypt.h
 * @brief Streaming firmware decryption (AES-256-GCM)
 *
 * Provides decrypt-in-place for encrypted firmware updates.
 * Key is retrieved from OTP/eFuse via the HAL.
 */

#ifndef EOS_FW_DECRYPT_H
#define EOS_FW_DECRYPT_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_AES_KEY_SIZE   32
#define EOS_AES_IV_SIZE    12
#define EOS_AES_TAG_SIZE   16
#define EOS_AES_BLOCK_SIZE 16

typedef struct {
    uint8_t  key[EOS_AES_KEY_SIZE];
    uint8_t  iv[EOS_AES_IV_SIZE];
    uint8_t  tag[EOS_AES_TAG_SIZE];
    uint8_t  ghash_h[EOS_AES_BLOCK_SIZE];   /* GHASH subkey H = AES_K(0) */
    uint8_t  ghash_acc[EOS_AES_BLOCK_SIZE];  /* Running GHASH accumulator */
    uint32_t bytes_processed;
    bool     initialized;
} eos_fw_decrypt_ctx_t;

/**
 * @brief Initialize decryption context with key from OTP and IV from image.
 * @param ctx  Decryption context (caller-allocated).
 * @param iv   Initialization vector from the image header/TLV.
 * @return EOS_OK on success, EOS_ERR_NOT_SUPPORTED if no HW crypto.
 */
int eos_fw_decrypt_init(eos_fw_decrypt_ctx_t *ctx, const uint8_t *iv);

/**
 * @brief Decrypt a chunk of data in-place.
 * @param ctx  Decryption context.
 * @param data Data buffer (decrypted in-place).
 * @param len  Length of data.
 * @return EOS_OK on success.
 */
int eos_fw_decrypt_update(eos_fw_decrypt_ctx_t *ctx, uint8_t *data, size_t len);

/**
 * @brief Finalize decryption and verify authentication tag.
 * @param ctx          Decryption context.
 * @param expected_tag Expected GCM authentication tag.
 * @return EOS_OK if tag matches, EOS_ERR_DECRYPT on failure.
 */
int eos_fw_decrypt_final(eos_fw_decrypt_ctx_t *ctx,
                         const uint8_t expected_tag[EOS_AES_TAG_SIZE]);

/**
 * @brief Securely wipe decryption context.
 */
void eos_fw_decrypt_cleanup(eos_fw_decrypt_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
#endif /* EOS_FW_DECRYPT_H */
