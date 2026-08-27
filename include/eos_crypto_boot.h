// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_crypto_boot.h
 * @brief Embedded crypto for secure boot — SHA-256 + signature verification
 */

#ifndef EOS_CRYPTO_BOOT_H
#define EOS_CRYPTO_BOOT_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_SHA256_BLOCK_SIZE  64
#define EOS_SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[EOS_SHA256_BLOCK_SIZE];
} eos_sha256_ctx_t;

void eos_sha256_init(eos_sha256_ctx_t *ctx);
void eos_sha256_update(eos_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void eos_sha256_final(eos_sha256_ctx_t *ctx, uint8_t digest[EOS_SHA256_DIGEST_SIZE]);

#define EOS_SHA512_BLOCK_SIZE  128
#define EOS_SHA512_DIGEST_SIZE 64

typedef struct {
    uint64_t state[8];
    uint64_t count;
    uint8_t  buffer[EOS_SHA512_BLOCK_SIZE];
} eos_sha512_ctx_t;

/**
 * SHA-512 (FIPS 180-4). Required by Ed25519 (RFC 8032), which derives its
 * challenge scalar from this hash; a verifier using any other hash cannot
 * check a signature produced by a conforming signer.
 */
void eos_sha512_init(eos_sha512_ctx_t *ctx);
void eos_sha512_update(eos_sha512_ctx_t *ctx, const uint8_t *data, size_t len);
void eos_sha512_final(eos_sha512_ctx_t *ctx, uint8_t digest[EOS_SHA512_DIGEST_SIZE]);

/** One-shot SHA-512 over a memory region. */
void eos_sha512(const uint8_t *data, size_t len,
                uint8_t digest[EOS_SHA512_DIGEST_SIZE]);

/** One-shot SHA-256 over a memory region. */
void eos_sha256(const void *data, size_t len,
                uint8_t digest[EOS_SHA256_DIGEST_SIZE]);

/**
 * @brief Verify an Ed25519 signature (RFC 8032).
 *
 * Declared here so every caller shares one prototype. Two translation units
 * previously each declared their own `extern`, and they disagreed on
 * argument order -- a mismatch no compiler could see.
 *
 * @param signature  64-byte signature, R || S.
 * @param public_key 32-byte Ed25519 public key.
 * @param message    Message bytes that were signed.
 * @param msg_len    Length of @p message in bytes.
 * @return EOS_OK if valid, EOS_ERR_SIGNATURE if not, EOS_ERR_INVALID on a
 *         null argument.
 */
int eos_ed25519_verify(const uint8_t signature[64],
                       const uint8_t public_key[32],
                       const uint8_t *message, size_t msg_len);

/**
 * Compute SHA-256 hash of a memory region.
 */
int eos_crypto_hash(const uint8_t *data, size_t len,
                     uint8_t digest[EOS_SHA256_DIGEST_SIZE]);

/**
 * Verify an image's integrity using its embedded hash.
 */
int eos_crypto_verify_image(uint32_t image_addr, uint32_t image_size,
                             const uint8_t expected_hash[EOS_SHA256_DIGEST_SIZE]);

/**
 * Verify a digital signature (Ed25519 — RFC 8032).
 */
int eos_crypto_verify_signature(const uint8_t *data, size_t data_len,
                                 const uint8_t *signature, size_t sig_len,
                                 const uint8_t *public_key, size_t key_len);

#ifdef __cplusplus
}
#endif

#endif /* EOS_CRYPTO_BOOT_H */
