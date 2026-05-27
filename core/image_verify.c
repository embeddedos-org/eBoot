// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file image_verify.c
 * @brief Image header parsing, integrity, signature, and anti-rollback
 *
 * Phase 2 implementation: SHA-256 hash verification, Ed25519 signature
 * dispatch, and monotonic anti-rollback counter checks.
 */

#include "eos_image.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include <string.h>

/* Forward declaration */
extern int eos_crypto_safe_compare(const uint8_t *a, const uint8_t *b, size_t len);

/* CRC32 computation — no lookup table needed */
static uint32_t crc32_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
        else         crc >>= 1;
    }
    return crc;
}

uint32_t eos_crc32(uint32_t addr, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[256];

    while (len > 0) {
        size_t chunk = (len > sizeof(buf)) ? sizeof(buf) : len;

        if (eos_hal_flash_read(addr, buf, chunk) != EOS_OK)
            return 0;

        for (size_t i = 0; i < chunk; i++) {
            crc = crc32_byte(crc, buf[i]);
        }

        addr += (uint32_t)chunk;
        len -= chunk;
    }

    return ~crc;
}

int eos_image_parse_header(uint32_t addr, eos_image_header_t *out)
{
    if (!out)
        return EOS_ERR_INVALID;

    int rc = eos_hal_flash_read(addr, out, sizeof(*out));
    if (rc != EOS_OK)
        return EOS_ERR_FLASH;

    if (out->magic != EOS_IMG_MAGIC)
        return EOS_ERR_NO_IMAGE;

    if (out->hdr_size < sizeof(eos_image_header_t) || out->hdr_size > 4096)
        return EOS_ERR_INVALID;

    if (out->image_size == 0 || out->image_size > 16 * 1024 * 1024)
        return EOS_ERR_INVALID;

    uint32_t payload_start = addr + out->hdr_size;
    uint32_t payload_end = payload_start + out->image_size;
    if (out->entry_addr < payload_start || out->entry_addr >= payload_end)
        return EOS_ERR_INVALID;

    return EOS_OK;
}

int eos_image_verify_integrity(const eos_image_header_t *hdr, uint32_t addr)
{
    if (!hdr)
        return EOS_ERR_INVALID;

    uint32_t payload_addr = addr + hdr->hdr_size;

    /* SHA-256 verification when flag is set */
    if (hdr->flags & EOS_IMG_FLAG_HASH_SHA256) {
        int rc = eos_crypto_verify_image(payload_addr, hdr->image_size, hdr->hash);
        /* Double-check for fault injection resistance */
        int rc2 = eos_crypto_verify_image(payload_addr, hdr->image_size, hdr->hash);
        if (rc != EOS_OK || rc2 != EOS_OK)
            return EOS_ERR_CRC;
        return EOS_OK;
    }

    /* CRC32 fallback */
    uint32_t computed_crc = eos_crc32(payload_addr, hdr->image_size);

    uint32_t stored_crc;
    memcpy(&stored_crc, hdr->hash, sizeof(stored_crc));

    if (computed_crc != stored_crc)
        return EOS_ERR_CRC;

    return EOS_OK;
}

int eos_image_verify_signature(const eos_image_header_t *hdr)
{
    if (!hdr)
        return EOS_ERR_INVALID;

    /* Only accept signed images in production */
    if (hdr->sig_type == EOS_SIG_NONE || hdr->sig_type == EOS_SIG_CRC32 || hdr->sig_type == EOS_SIG_SHA256)
        return EOS_ERR_SIGNATURE;

    if (hdr->sig_len == 0 || hdr->sig_len > EOS_SIG_MAX_SIZE)
        return EOS_ERR_SIGNATURE;

    /* Phase 2: Ed25519 signature verification */
    if (hdr->sig_type == EOS_SIG_ED25519) {
        /* Get public key from keystore */
        extern int eos_keystore_get_compiled_key(const uint8_t **key, size_t *len);
        const uint8_t *pub_key = NULL;
        size_t key_len = 0;

        /* Try compiled-in key directly */
        extern const uint8_t ebldr_default_pubkey[32];
        pub_key = ebldr_default_pubkey;
        key_len = 32;

        /* Verify signature over the hash */
        int rc = eos_crypto_verify_signature(
            hdr->hash, EOS_HASH_SIZE,
            hdr->signature, hdr->sig_len,
            pub_key, key_len);

        /* Double-check for fault injection resistance */
        int rc2 = eos_crypto_verify_signature(
            hdr->hash, EOS_HASH_SIZE,
            hdr->signature, hdr->sig_len,
            pub_key, key_len);

        if (rc != EOS_OK || rc2 != EOS_OK)
            return EOS_ERR_SIGNATURE;

        return EOS_OK;
    }

    /* Unknown signature type */
    return EOS_ERR_SIGNATURE;
}

int eos_image_check_version(uint32_t candidate_version, uint32_t min_version)
{
    if (candidate_version < min_version)
        return EOS_ERR_VERSION;

    return EOS_OK;
}

int eos_image_check_rollback(uint32_t candidate_version)
{
    /* Read monotonic counter from hardware */
    uint32_t hw_min_version = 0;
    int rc = eos_hal_monotonic_read(&hw_min_version);
    if (rc == EOS_ERR_NOT_SUPPORTED) {
        /* No HW counter — version check only */
        return EOS_OK;
    }
    if (rc != EOS_OK) {
        return rc;
    }

    if (candidate_version < hw_min_version) {
        return EOS_ERR_ANTI_ROLLBACK;
    }

    return EOS_OK;
}
