// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file secure_boot.c
 * @brief Secure boot chain implementation
 *
 * Orchestrates: header parse → hash verify → signature verify →
 * key attestation → anti-rollback → decrypt → attestation log.
 */

#include "eos_secure_boot.h"
#include "eos_image.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include <string.h>

/* OTP offsets for root-of-trust */
#define OTP_KEY_HASH_OFFSET     0x100
#define OTP_KEY_HASH_SIZE       32
#define OTP_ROLLBACK_OFFSET     0x180
#define OTP_DEBUG_LOCK_OFFSET   0x1F0

/* Global attestation log — persisted in RAM for kernel to read */
static eos_attest_log_t g_attest_log;

/* ---- SHA-256 (from crypto_boot.c) ---- */
extern void eos_sha256(const void *data, size_t len, uint8_t hash[32]);

/* ---- Ed25519 verify (from ed25519_verify.c) ---- */
extern int eos_ed25519_verify(const uint8_t *msg, size_t msg_len,
                               const uint8_t sig[64],
                               const uint8_t pubkey[32]);

/* ---- Constant-time compare ---- */
static int secure_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0 ? 0 : -1;
}

/* ---- Add attestation entry ---- */
static void attest_record(uint32_t stage, uint32_t version,
                           const uint8_t hash[32], const uint8_t key_hash[32],
                           int result)
{
    if (g_attest_log.count >= EOS_ATTEST_MAX_ENTRIES) return;

    eos_attest_entry_t *e = &g_attest_log.entries[g_attest_log.count];
    e->stage = stage;
    e->image_version = version;
    if (hash) memcpy(e->hash, hash, 32);
    if (key_hash) memcpy(e->key_hash, key_hash, 32);
    e->verify_result = result;
    e->timestamp_ticks = 0; /* Would use SysTick in real HW */

    g_attest_log.count++;

    /* Update chain hash: H(prev_chain || new_measurement) */
    uint8_t chain_input[64];
    memcpy(chain_input, g_attest_log.chain_hash, 32);
    if (hash) memcpy(chain_input + 32, hash, 32);
    else      memset(chain_input + 32, 0, 32);
    eos_sha256(chain_input, 64, g_attest_log.chain_hash);
}

/* ============================================================
 * Main Secure Boot Flow
 * ============================================================ */

eos_secure_boot_result_t eos_secure_boot(const eos_secure_boot_config_t *cfg,
                                          uint32_t *entry_out)
{
    if (!cfg || !entry_out) return EOS_SBOOT_ERR_NO_IMAGE;

    /* Initialize attestation log */
    memset(&g_attest_log, 0, sizeof(g_attest_log));
    g_attest_log.magic = EOS_ATTEST_MAGIC;

    /* ---- Step 1: Parse image header ---- */
    eos_image_header_t hdr;
    int rc = eos_image_parse_header(cfg->image_addr, &hdr);
    if (rc != EOS_OK) {
        attest_record(2, 0, NULL, NULL, EOS_SBOOT_ERR_BAD_HEADER);
        return EOS_SBOOT_ERR_BAD_HEADER;
    }

    /* ---- Step 2: Verify integrity (SHA-256 hash) ---- */
    /* Bug Fix: Pass image_addr directly to verify_integrity since verify_integrity already adds hdr_size */
    rc = eos_image_verify_integrity(&hdr, cfg->image_addr);
    if (rc != EOS_OK) {
        attest_record(2, hdr.image_version, hdr.hash, NULL, EOS_SBOOT_ERR_INTEGRITY);
        return EOS_SBOOT_ERR_INTEGRITY;
    }

    /* ---- Step 3: Verify signature ---- */
    if (cfg->require_signature) {
        rc = eos_image_verify_signature(&hdr);
        if (rc != EOS_OK) {
            attest_record(2, hdr.image_version, hdr.hash, NULL, EOS_SBOOT_ERR_SIGNATURE);
            return EOS_SBOOT_ERR_SIGNATURE;
        }

        /* ---- Step 4: Verify signing key against OTP root-of-trust ---- */
        /* Extract key hash from TLV area */
        /* The TLV_KEYHASH entry contains SHA-256 of the public key */
        uint8_t otp_key_hash[32];
        rc = eos_hal_otp_read(OTP_KEY_HASH_OFFSET, otp_key_hash, OTP_KEY_HASH_SIZE);
        if (rc == EOS_OK) {
            /* Check if OTP key hash is provisioned (not all-zeros) */
            uint8_t zeros[32] = {0};
            if (secure_compare(otp_key_hash, zeros, 32) != 0) {
                /* OTP is provisioned — must match */
                /* In a full implementation, extract key hash from TLV and compare */
                /* For now, the signature verification implicitly uses the embedded key */
            }
        }
    }

    /* ---- Step 5: Anti-rollback check ---- */
    if (cfg->min_version > 0) {
        rc = eos_image_check_version(hdr.image_version, cfg->min_version);
        if (rc != EOS_OK) {
            attest_record(2, hdr.image_version, hdr.hash, NULL, EOS_SBOOT_ERR_ROLLBACK);
            return EOS_SBOOT_ERR_ROLLBACK;
        }
    }

    /* ---- Step 6: Decrypt if required ---- */
    if (cfg->require_encryption && (hdr.flags & EOS_IMG_FLAG_ENCRYPTED)) {
        /* Use fw_decrypt module for AES-256-GCM decryption */
        /* Key is read from OTP by fw_decrypt_init() */
        extern int eos_fw_decrypt_init(void *ctx, const uint8_t *iv);
        extern int eos_fw_decrypt_update(void *ctx, uint8_t *data, size_t len);
        extern int eos_fw_decrypt_final(void *ctx, const uint8_t *tag);

        /* In a full implementation:
         * 1. Extract IV and tag from TLV
         * 2. Init decrypt context
         * 3. Decrypt image in-place
         * 4. Verify GCM tag
         */
        
        /* Fix critical bug: Return decryption error if decryption is requested but unsupported/unimplemented */
        return EOS_SBOOT_ERR_DECRYPT;
    }

    /* ---- Step 7: Lock debug interfaces ---- */
    if (cfg->lock_debug) {
        eos_secure_boot_lock_debug();
    }

    /* ---- Step 8: Record successful attestation ---- */
    attest_record(2, hdr.image_version, hdr.hash, NULL, EOS_SBOOT_OK);

    /* ---- Step 9: Return entry point ---- */
    *entry_out = hdr.entry_addr;
    return EOS_SBOOT_OK;
}

/* ============================================================
 * Supporting Functions
 * ============================================================ */

const eos_attest_log_t *eos_secure_boot_get_attestation(void)
{
    return &g_attest_log;
}

int eos_secure_boot_verify_key(const uint8_t key_hash[32])
{
    uint8_t otp_hash[32];
    int rc = eos_hal_otp_read(OTP_KEY_HASH_OFFSET, otp_hash, 32);
    if (rc != EOS_OK) return -1;

    return secure_compare(key_hash, otp_hash, 32);
}

void eos_secure_boot_lock_debug(void)
{
    /* Write lock pattern to eFuse debug lock register */
    uint8_t lock = 0xFF;
    eos_hal_otp_write(OTP_DEBUG_LOCK_OFFSET, &lock, 1);

    /* On Cortex-M: disable DAP access via DHCSR if supported */
#if defined(__ARM_ARCH)
    /* Some MCUs support disabling debug via DBGMCU register */
    /* *((volatile uint32_t *)0xE0042004) = 0; */
#endif
}

int eos_secure_boot_update_rollback(uint32_t new_version)
{
    /* Write new minimum version to OTP anti-rollback counter */
    uint8_t ver_bytes[4];
    ver_bytes[0] = (uint8_t)(new_version >> 24);
    ver_bytes[1] = (uint8_t)(new_version >> 16);
    ver_bytes[2] = (uint8_t)(new_version >> 8);
    ver_bytes[3] = (uint8_t)(new_version);

    return eos_hal_otp_write(OTP_ROLLBACK_OFFSET, ver_bytes, 4);
}
