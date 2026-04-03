// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file keystore.c
 * @brief Public key storage and management for secure boot
 *
 * Manages Ed25519 public keys for image signature verification.
 * Supports compiled-in keys and OTP-backed retrieval with dual
 * key slots for rotation.
 */

#include "eos_keystore.h"
#include "eos_hal.h"
#include <string.h>

/* Default development key — REPLACE with production key before deployment.
 * This is the public half of a well-known test keypair.
 * Production builds MUST set EBLDR_PRODUCTION_KEY at compile time. */
#ifndef EBLDR_PRODUCTION_KEY
static const uint8_t default_dev_key[EOS_ED25519_PUB_KEY_SIZE] = {
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
    0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa3, 0xf4, 0xa1,
    0x8c, 0x42, 0xc4, 0x76, 0x84, 0x37, 0x77, 0x25,
};
#endif

/* OTP offset where keys are stored */
#define OTP_KEY_OFFSET_SLOT0  0x100
#define OTP_KEY_OFFSET_SLOT1  0x120
#define OTP_REVOKE_OFFSET     0x140

/**
 * @brief Constant-time comparison to avoid timing side-channels.
 */
static int safe_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return (diff == 0) ? 0 : -1;
}

int eos_keystore_init(eos_keystore_t *ks)
{
    if (!ks) return EOS_ERR_INVALID;

    memset(ks, 0, sizeof(*ks));

    /* Try OTP first */
    int rc = eos_hal_otp_read(OTP_KEY_OFFSET_SLOT0,
                              ks->slots[0].key,
                              EOS_ED25519_PUB_KEY_SIZE);
    if (rc == EOS_OK) {
        /* Check if key is non-zero (provisioned) */
        uint8_t zero[EOS_ED25519_PUB_KEY_SIZE] = {0};
        if (safe_compare(ks->slots[0].key, zero, EOS_ED25519_PUB_KEY_SIZE) != 0) {
            ks->slots[0].valid = true;
            ks->source = EOS_KEY_SOURCE_OTP;
        }

        /* Try slot 1 */
        rc = eos_hal_otp_read(OTP_KEY_OFFSET_SLOT1,
                              ks->slots[1].key,
                              EOS_ED25519_PUB_KEY_SIZE);
        if (rc == EOS_OK &&
            safe_compare(ks->slots[1].key, zero, EOS_ED25519_PUB_KEY_SIZE) != 0) {
            ks->slots[1].valid = true;
        }

        /* Check revocation status */
        uint8_t revoke_flags = 0;
        if (eos_hal_otp_read(OTP_REVOKE_OFFSET, &revoke_flags, 1) == EOS_OK) {
            if (revoke_flags & 0x01) ks->slots[0].revoked = true;
            if (revoke_flags & 0x02) ks->slots[1].revoked = true;
        }
    }

    /* Fall back to compiled-in key if OTP not available */
    if (!ks->slots[0].valid && !ks->slots[1].valid) {
#ifndef EBLDR_PRODUCTION_KEY
        memcpy(ks->slots[0].key, default_dev_key, EOS_ED25519_PUB_KEY_SIZE);
#else
        extern const uint8_t ebldr_production_key[EOS_ED25519_PUB_KEY_SIZE];
        memcpy(ks->slots[0].key, ebldr_production_key, EOS_ED25519_PUB_KEY_SIZE);
#endif
        ks->slots[0].valid = true;
        ks->source = EOS_KEY_SOURCE_COMPILED;
    }

    /* Set active slot to first valid, non-revoked slot */
    ks->active_slot = EOS_KEY_SLOTS; /* invalid sentinel */
    for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
        if (ks->slots[i].valid && !ks->slots[i].revoked) {
            ks->active_slot = i;
            break;
        }
    }

    /* Read monotonic security version */
    uint32_t sec_ver = 0;
    if (eos_hal_monotonic_read(&sec_ver) == EOS_OK) {
        for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
            ks->slots[i].security_version = sec_ver;
        }
    }

    return EOS_OK;
}

int eos_keystore_get_active_key(const eos_keystore_t *ks,
                                const uint8_t **key_out,
                                size_t *key_len)
{
    if (!ks || !key_out || !key_len) return EOS_ERR_INVALID;

    if (ks->active_slot >= EOS_KEY_SLOTS) return EOS_ERR_KEY;

    const eos_key_slot_t *slot = &ks->slots[ks->active_slot];
    if (!slot->valid || slot->revoked) return EOS_ERR_KEY;

    *key_out = slot->key;
    *key_len = EOS_ED25519_PUB_KEY_SIZE;
    return EOS_OK;
}

int eos_keystore_find_by_hash(const eos_keystore_t *ks,
                              const uint8_t key_hash[EOS_SHA256_DIGEST_SIZE],
                              uint32_t *slot_out)
{
    if (!ks || !key_hash || !slot_out) return EOS_ERR_INVALID;

    for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
        if (!ks->slots[i].valid || ks->slots[i].revoked) continue;

        /* Compute SHA-256 of the stored key */
        uint8_t computed_hash[EOS_SHA256_DIGEST_SIZE];
        eos_crypto_hash(ks->slots[i].key, EOS_ED25519_PUB_KEY_SIZE, computed_hash);

        if (safe_compare(computed_hash, key_hash, EOS_SHA256_DIGEST_SIZE) == 0) {
            *slot_out = i;
            return EOS_OK;
        }
    }

    return EOS_ERR_NOT_FOUND;
}

int eos_keystore_check_security_version(const eos_keystore_t *ks,
                                        uint32_t security_version)
{
    if (!ks) return EOS_ERR_INVALID;

    /* Read current monotonic counter */
    uint32_t current = 0;
    int rc = eos_hal_monotonic_read(&current);
    if (rc == EOS_ERR_NOT_SUPPORTED) {
        /* No HW counter — fall back to stored version */
        if (ks->active_slot < EOS_KEY_SLOTS) {
            current = ks->slots[ks->active_slot].security_version;
        }
    } else if (rc != EOS_OK) {
        return rc;
    }

    if (security_version < current) {
        return EOS_ERR_ANTI_ROLLBACK;
    }

    return EOS_OK;
}

int eos_keystore_revoke_slot(eos_keystore_t *ks, uint32_t slot)
{
    if (!ks || slot >= EOS_KEY_SLOTS) return EOS_ERR_INVALID;

    ks->slots[slot].revoked = true;

    /* Try to persist revocation to OTP */
    uint8_t revoke_flags = 0;
    eos_hal_otp_read(OTP_REVOKE_OFFSET, &revoke_flags, 1);
    revoke_flags |= (1U << slot);
    eos_hal_otp_write(OTP_REVOKE_OFFSET, &revoke_flags, 1);

    /* Update active slot */
    ks->active_slot = EOS_KEY_SLOTS;
    for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
        if (ks->slots[i].valid && !ks->slots[i].revoked) {
            ks->active_slot = i;
            break;
        }
    }

    return EOS_OK;
}


int eos_keystore_key_count(const eos_keystore_t *ks, uint32_t *count_out)
{
    if (!ks || !count_out) return EOS_ERR_INVALID;

    uint32_t count = 0;
    for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
        if (ks->slots[i].valid && !ks->slots[i].revoked) {
            count++;
        }
    }
    *count_out = count;
    return EOS_OK;
}

int eos_keystore_get_security_version(const eos_keystore_t *ks,
                                      uint32_t *version_out)
{
    if (!ks || !version_out) return EOS_ERR_INVALID;
    if (ks->active_slot >= EOS_KEY_SLOTS) return EOS_ERR_KEY;

    *version_out = ks->slots[ks->active_slot].security_version;
    return EOS_OK;
}

int eos_keystore_set_security_version(eos_keystore_t *ks, uint32_t version)
{
    if (!ks) return EOS_ERR_INVALID;
    if (ks->active_slot >= EOS_KEY_SLOTS) return EOS_ERR_KEY;

    if (version < ks->slots[ks->active_slot].security_version) {
        return EOS_ERR_ANTI_ROLLBACK;
    }

    for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
        ks->slots[i].security_version = version;
    }
    return EOS_OK;
}