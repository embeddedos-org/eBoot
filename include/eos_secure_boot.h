// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file eos_secure_boot.h
 * @brief Secure boot chain orchestration for eBoot
 *
 * Ties together image verification, signature checking, anti-rollback,
 * key attestation, and boot logging into a single secure boot flow.
 */

#ifndef EOS_SECURE_BOOT_H
#define EOS_SECURE_BOOT_H

#include "eos_types.h"
#include "eos_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Secure Boot Configuration ---- */

typedef struct {
    uint32_t image_addr;            /* Flash address of image header */
    uint32_t min_version;           /* Anti-rollback minimum version */
    bool     require_signature;     /* Enforce signature verification */
    bool     require_encryption;    /* Enforce AES-GCM decryption */
    bool     lock_debug;            /* Disable SWD/JTAG after boot */
    bool     enable_attestation;    /* Log boot measurements */
} eos_secure_boot_config_t;

/* ---- Secure Boot Result ---- */

typedef enum {
    EOS_SBOOT_OK                = 0,
    EOS_SBOOT_ERR_NO_IMAGE      = -1,
    EOS_SBOOT_ERR_BAD_MAGIC     = -2,
    EOS_SBOOT_ERR_BAD_HEADER    = -3,
    EOS_SBOOT_ERR_INTEGRITY     = -4,  /* Hash mismatch */
    EOS_SBOOT_ERR_SIGNATURE     = -5,  /* Signature invalid */
    EOS_SBOOT_ERR_KEY_MISMATCH  = -6,  /* Key hash doesn't match OTP */
    EOS_SBOOT_ERR_ROLLBACK      = -7,  /* Version too old */
    EOS_SBOOT_ERR_DECRYPT       = -8,  /* Decryption failed */
    EOS_SBOOT_ERR_POLICY        = -9,  /* Boot policy violation */
} eos_secure_boot_result_t;

/* ---- Boot Attestation Record ---- */

#define EOS_ATTEST_MAX_ENTRIES  8

typedef struct {
    uint32_t stage;                 /* Boot stage (0=ROM, 1=stage0, 2=stage1, 3=kernel) */
    uint32_t image_version;         /* Image version that was verified */
    uint8_t  hash[32];              /* SHA-256 measurement of the image */
    uint8_t  key_hash[32];          /* SHA-256 of the signing key used */
    int      verify_result;         /* 0=pass, negative=error code */
    uint32_t timestamp_ticks;       /* SysTick count at verification time */
} eos_attest_entry_t;

typedef struct {
    uint32_t magic;                 /* 0x41545354 = "ATST" */
    uint32_t count;
    eos_attest_entry_t entries[EOS_ATTEST_MAX_ENTRIES];
    uint8_t  chain_hash[32];        /* Running hash of all measurements */
} eos_attest_log_t;

#define EOS_ATTEST_MAGIC    0x41545354U

/* ---- API ---- */

/**
 * @brief Execute the full secure boot chain.
 *
 * Sequence:
 *   1. Parse image header at image_addr
 *   2. Verify header magic and format version
 *   3. Compute SHA-256 of image payload, compare to header hash
 *   4. Verify Ed25519 signature over the hash
 *   5. Check signing key hash against OTP root-of-trust
 *   6. Check image version against anti-rollback counter
 *   7. Decrypt image if encrypted (AES-256-GCM)
 *   8. Record attestation measurements
 *   9. Return entry point on success
 *
 * @param cfg       Secure boot configuration.
 * @param entry_out On success, populated with the image entry point.
 * @return EOS_SBOOT_OK on success, error code on failure.
 */
eos_secure_boot_result_t eos_secure_boot(const eos_secure_boot_config_t *cfg,
                                          uint32_t *entry_out);

/**
 * @brief Get the boot attestation log.
 * @return Pointer to the attestation log (valid after eos_secure_boot).
 */
const eos_attest_log_t *eos_secure_boot_get_attestation(void);

/**
 * @brief Verify the root-of-trust key hash against OTP.
 * @param key_hash  SHA-256 hash of the signing public key.
 * @return 0 if matches OTP, -1 if mismatch.
 */
int eos_secure_boot_verify_key(const uint8_t key_hash[32]);

/**
 * @brief Lock debug interfaces (SWD/JTAG) permanently.
 * Only effective on real hardware with eFuse support.
 */
void eos_secure_boot_lock_debug(void);

/**
 * @brief Update the anti-rollback counter in OTP.
 * @param new_version  New minimum version to enforce.
 * @return 0 on success, -1 if OTP write fails.
 */
int eos_secure_boot_update_rollback(uint32_t new_version);

#ifdef __cplusplus
}
#endif
#endif /* EOS_SECURE_BOOT_H */
