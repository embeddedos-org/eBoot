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
#ifdef EBLDR_PRODUCTION_KEY
#include "eos_production_key.h"
#endif

/* The compiled-in trust anchor, used when the board has no OTP to read a
 * key from -- which today is every board under boards/.
 *
 * Without EBLDR_PRODUCTION_KEY it is the public half of TEST 1 in RFC 8032
 * section 7.1. The matching private key is printed in the RFC, so anyone at
 * all can produce a signature that a bootloader trusting this key will
 * accept. It is a usable default for bring-up and for the unit tests, and it
 * must never reach a device. The #warning below is deliberate: this key going
 * out silently is the failure mode, so a build that embeds it says so on
 * every compile.
 *
 * A production key is supplied at configure time:
 *
 *     cmake -B build -DEBLDR_BOARD=<board> -DCMAKE_BUILD_TYPE=Release \
 *           -DEBLDR_PRODUCTION_KEY=<64 hex characters, the raw Ed25519 public key>
 *
 * cmake/ProductionKey.cmake turns that into a generated translation unit
 * defining ebldr_production_key[] (declared in eos_production_key.h),
 * compiles it into eboot_core and defines EBLDR_PRODUCTION_KEY, which selects
 * the #else branch below. A Release build of a real board that sets no key
 * refuses to configure unless it also sets EBLDR_ALLOW_DEV_KEY=ON, and
 * .github/workflows/release.yml refuses any artifact that still contains the
 * development key's bytes. */
#ifndef EBLDR_PRODUCTION_KEY
#warning "eBoot: building with the RFC 8032 test-vector public key as the secure-boot trust anchor; define EBLDR_PRODUCTION_KEY for any real device"
/* RFC 8032 section 7.1, TEST 1, PUBLIC KEY -- all 32 bytes of it. The array
 * shipped from v0.1.0 to here agreed with the RFC for 21 bytes and then did
 * not, and the result was not a point on the curve: no signature could ever
 * verify against it, on any board that fell back to it. See
 * tests/unit/test_keystore.c::test_compiled_in_anchor_verifies_its_own_rfc_vector. */
static const uint8_t default_dev_key[EOS_ED25519_PUB_KEY_SIZE] = {
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
    0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
    0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
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

/* A slot holds a provisioned key only when its bytes are neither all-zero
 * nor all-0xFF: unprogrammed OTP reads back as either, and both are public.
 * Same treatment as the recovery secret in core/recovery.c. */
static bool slot_is_provisioned(const uint8_t *key)
{
    const uint8_t zero[EOS_ED25519_PUB_KEY_SIZE] = {0};
    uint8_t erased[EOS_ED25519_PUB_KEY_SIZE];
    memset(erased, 0xFF, sizeof(erased));
    return safe_compare(key, zero, EOS_ED25519_PUB_KEY_SIZE) != 0 &&
           safe_compare(key, erased, EOS_ED25519_PUB_KEY_SIZE) != 0;
}

int eos_keystore_init(eos_keystore_t *ks)
{
    if (!ks) return EOS_ERR_INVALID;

    memset(ks, 0, sizeof(*ks));

    /* Try OTP first.
     *
     * EOS_ERR_NOT_SUPPORTED means this board has no OTP at all, which is a
     * legitimate configuration: the trust anchor is then the compiled-in key.
     * Any other error means the board HAS an OTP and reading it failed, so the
     * key material this device is provisioned with is unknown. Those two are
     * not interchangeable, and treating them alike let a fault on the OTP bus
     * silently swap the trust anchor for whatever was compiled in. */
    int rc = eos_hal_otp_read(OTP_KEY_OFFSET_SLOT0,
                              ks->slots[0].key,
                              EOS_ED25519_PUB_KEY_SIZE);
    bool otp_present = (rc != EOS_ERR_NOT_SUPPORTED);

    if (otp_present && rc != EOS_OK)
        return EOS_ERR_KEY;

    if (rc == EOS_OK) {
        /* A slot counts as provisioned only when it holds a real key.
         * Unprogrammed OTP reads back as all zeros or all ones, and both
         * are public values, so neither is a key anyone provisioned. The
         * old check accepted all-0xFF, so on a single-key device the
         * never-written slot 1 read as a second provisioned key -- and
         * once slot 0 was revoked, the erased slot became the active key
         * and get_active_key() handed back 0xFF bytes instead of
         * EOS_ERR_KEY. The erased revoke byte used to hide this by
         * revoking both slots; clear-to-revoke (#183) removed that mask.
         * Same treatment as the recovery secret in core/recovery.c. */
        if (slot_is_provisioned(ks->slots[0].key)) {
            ks->slots[0].valid = true;
            ks->source = EOS_KEY_SOURCE_OTP;
        }

        /* Try slot 1 */
        rc = eos_hal_otp_read(OTP_KEY_OFFSET_SLOT1,
                              ks->slots[1].key,
                              EOS_ED25519_PUB_KEY_SIZE);
        if (rc != EOS_OK && rc != EOS_ERR_NOT_SUPPORTED)
            return EOS_ERR_KEY;
        if (rc == EOS_OK && slot_is_provisioned(ks->slots[1].key)) {
            ks->slots[1].valid = true;
        }

        /* Revocation status.
         *
         * Clear-to-revoke (#183): a burned (0) bit revokes the slot, and
         * the erased byte (0xFF) revokes nothing. OTP bits only clear, so
         * this is the only encoding a revocation can persist in -- the old
         * set-to-revoke reading asked stm32f4_otp_write() to set a bit,
         * which it refuses, and read an erased byte as both slots revoked.
         * Provisioning leaves the byte erased, which is the unrevoked
         * state; nothing has to be written first.
         *
         * If this read fails we cannot show that a key has NOT been revoked.
         * Leaving the flags clear is the wrong default: revocation exists to
         * retire a key whose private half is believed compromised, so a device
         * that cannot read the revocation store must not keep using the keys it
         * covers. Treat every OTP slot as revoked; eos_keystore_get_active_key()
         * then reports EOS_ERR_KEY rather than handing back a key that may have
         * been retired. */
        uint8_t revoke_flags = 0;
        if (eos_hal_otp_read(OTP_REVOKE_OFFSET, &revoke_flags, 1) == EOS_OK) {
            if (!(revoke_flags & 0x01)) ks->slots[0].revoked = true;
            if (!(revoke_flags & 0x02)) ks->slots[1].revoked = true;
        } else {
            for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++)
                ks->slots[i].revoked = true;
        }
    }

    /* Fall back to compiled-in key only when the board has no OTP to consult. */
    if (!ks->slots[0].valid && !ks->slots[1].valid && !otp_present) {
#ifndef EBLDR_PRODUCTION_KEY
        memcpy(ks->slots[0].key, default_dev_key, EOS_ED25519_PUB_KEY_SIZE);
#else
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
    } else {
        /* No HW counter available — default to version 1 so that
         * anti-rollback checks remain meaningful (version 0 would
         * allow any image to pass the security-version gate). */
        for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
            ks->slots[i].security_version = 1;
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

    /* Persist the revocation to OTP.
     *
     * Clear-to-revoke (#183): revoking burns the slot's bit. OTP bits only
     * clear, so the old read-OR-write asked the OTP to set a bit, which
     * stm32f4_otp_write() refuses -- the revocation stayed RAM-only while
     * the caller was told it failed to persist.
     *
     * The read must be checked before the AND: on failure revoke_flags
     * stayed 0, so writing it back would revoke every slot -- revoking
     * slot 1 would revoke slot 0 too. And a write that fails leaves the
     * revocation in RAM only, so it is gone at the next reset while this
     * function reported EOS_OK and the caller believed the key was
     * permanently retired. Both are reported now; the in-RAM revocation
     * stands either way, so this boot still refuses the key. */
    int persist_rc = EOS_OK;
    uint8_t revoke_flags = 0;
    int read_rc = eos_hal_otp_read(OTP_REVOKE_OFFSET, &revoke_flags, 1);
    if (read_rc == EOS_OK) {
        revoke_flags &= (uint8_t)~(1U << slot);
        persist_rc = eos_hal_otp_write(OTP_REVOKE_OFFSET, &revoke_flags, 1);
    } else if (read_rc == EOS_ERR_NOT_SUPPORTED) {
        /* No OTP on this board: nothing to persist to, and nothing to clobber. */
        persist_rc = EOS_OK;
    } else {
        persist_rc = read_rc;
    }

    /* Update active slot */
    ks->active_slot = EOS_KEY_SLOTS;
    for (uint32_t i = 0; i < EOS_KEY_SLOTS; i++) {
        if (ks->slots[i].valid && !ks->slots[i].revoked) {
            ks->active_slot = i;
            break;
        }
    }

    return persist_rc;
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
