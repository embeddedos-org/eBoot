// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_keystore.h
 * @brief Public key storage and management for secure boot
 *
 * Supports dual-key slots (primary + backup) for rotation,
 * compiled-in keys, and OTP/eFuse-backed retrieval.
 */

#ifndef EOS_KEYSTORE_H
#define EOS_KEYSTORE_H

#include "eos_types.h"
#include "eos_crypto_boot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_ED25519_PUB_KEY_SIZE  32
#define EOS_KEY_SLOTS             2

typedef enum {
    EOS_KEY_SOURCE_COMPILED = 0,
    EOS_KEY_SOURCE_OTP      = 1,
} eos_key_source_t;

typedef struct {
    uint8_t  key[EOS_ED25519_PUB_KEY_SIZE];
    bool     valid;
    bool     revoked;
    uint32_t security_version;
} eos_key_slot_t;

typedef struct {
    eos_key_slot_t   slots[EOS_KEY_SLOTS];
    eos_key_source_t source;
    uint32_t         active_slot;
} eos_keystore_t;

/**
 * @brief Initialize the keystore with compiled-in or OTP keys.
 * @param ks  Keystore context (caller-allocated).
 * @return EOS_OK on success.
 */
int eos_keystore_init(eos_keystore_t *ks);

/**
 * @brief Get the active public key for signature verification.
 * @param ks      Keystore context.
 * @param key_out Pointer to receive key data pointer.
 * @param key_len Pointer to receive key length.
 * @return EOS_OK if a valid key is available.
 */
int eos_keystore_get_active_key(const eos_keystore_t *ks,
                                const uint8_t **key_out,
                                size_t *key_len);

/**
 * @brief Check if a key hash matches one of the stored keys.
 * @param ks        Keystore context.
 * @param key_hash  SHA-256 hash of the signing public key.
 * @param slot_out  Receives the matching slot index.
 * @return EOS_OK if a match is found.
 */
int eos_keystore_find_by_hash(const eos_keystore_t *ks,
                              const uint8_t key_hash[EOS_SHA256_DIGEST_SIZE],
                              uint32_t *slot_out);

/**
 * @brief Check if the given security version is acceptable.
 * @param ks               Keystore context.
 * @param security_version Candidate security version.
 * @return EOS_OK if acceptable, EOS_ERR_ANTI_ROLLBACK if too old.
 */
int eos_keystore_check_security_version(const eos_keystore_t *ks,
                                        uint32_t security_version);

/**
 * @brief Revoke a key slot (marks it as unusable).
 * @param ks   Keystore context.
 * @param slot Slot index to revoke.
 * @return EOS_OK on success.
 */
int eos_keystore_revoke_slot(eos_keystore_t *ks, uint32_t slot);

#ifdef __cplusplus
}
#endif
#endif /* EOS_KEYSTORE_H */
