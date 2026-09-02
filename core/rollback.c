// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file rollback.c
 * @brief Anti-rollback security counter enforcement and commit.
 */

#include "eos_rollback.h"
#include "eos_image.h"
#include "eos_image_tlv.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include <string.h>

static uint32_t g_staged_counter;
static bool     g_staged_valid;

/**
 * Check that the TLV area at @p tlv_addr is the one the signed header vouches
 * for, by hashing hdr->tlv_len bytes of it and comparing against hdr->tlv_hash.
 *
 * Streams the area in small chunks rather than buffering it, so the stack cost
 * does not scale with EOS_TLV_MAX_SIZE.
 */
static int tlv_area_matches_header(const eos_image_header_t *hdr,
                                   uint32_t tlv_addr)
{
    eos_sha256_ctx_t sha;
    uint8_t chunk[64];
    uint8_t digest[EOS_SHA256_DIGEST_SIZE];
    uint32_t remaining = hdr->tlv_len;
    uint32_t addr = tlv_addr;

    eos_sha256_init(&sha);

    while (remaining > 0) {
        uint32_t n = (remaining > sizeof(chunk)) ? (uint32_t)sizeof(chunk)
                                                 : remaining;
        if (eos_hal_flash_read(addr, chunk, n) != EOS_OK)
            return EOS_ERR_FLASH;
        eos_sha256_update(&sha, chunk, n);
        addr += n;
        remaining -= n;
    }

    eos_sha256_final(&sha, digest);

    /* Constant-time: this compares attacker-influenced bytes, and a bootloader
     * offers unlimited retries to anyone with a logic analyser. */
    volatile uint8_t diff = 0;
    for (uint32_t i = 0; i < EOS_IMG_TLV_HASH_LEN; i++)
        diff |= (uint8_t)(digest[i] ^ hdr->tlv_hash[i]);

    return (diff == 0) ? EOS_OK : EOS_ERR_INVALID;
}

int eos_rollback_read_image_counter(uint32_t image_addr, uint32_t *counter_out)
{
    if (!counter_out) return EOS_ERR_INVALID;

    *counter_out = 0;

    eos_image_header_t hdr;
    int rc = eos_image_parse_header(image_addr, &hdr);
    if (rc != EOS_OK) return rc;

    /* The TLV area follows the header and payload. */
    uint32_t tlv_addr = image_addr;
    if (tlv_addr + hdr.hdr_size < tlv_addr) return EOS_ERR_INVALID;
    tlv_addr += hdr.hdr_size;
    if (tlv_addr + hdr.image_size < tlv_addr) return EOS_ERR_INVALID;
    tlv_addr += hdr.image_size;

    /* Nothing else in the image covers those bytes: the signature stops at
     * EOS_IMG_SIGNED_LEN and hash[] stops after image_size payload bytes. An
     * attacker able to write flash could otherwise take a genuinely signed old
     * image, raise its declared counter, and walk it straight past
     * eos_rollback_verify() — the downgrade anti-rollback exists to prevent.
     *
     * hdr.tlv_len and hdr.tlv_hash are inside the signed prefix, so they are
     * the only place a claim about this area can be trusted from.
     *
     * tlv_len == 0 means the image makes no such claim. The area is then
     * unauthenticated and is not read: reporting 0 is the conservative
     * reading, since a counter of 0 can only fail against the device floor,
     * never raise it. */
    if (hdr.tlv_len == 0)
        return EOS_OK;

    if (hdr.tlv_len < sizeof(eos_tlv_info_t) || hdr.tlv_len > EOS_TLV_MAX_SIZE)
        return EOS_ERR_INVALID;

    if (tlv_addr + hdr.tlv_len < tlv_addr)
        return EOS_ERR_INVALID;

    /* The header claims an authenticated area and the bytes do not match it.
     * That is tampering, not absence — fail closed rather than degrading to 0,
     * so the condition is reported instead of passing silently. */
    rc = tlv_area_matches_header(&hdr, tlv_addr);
    if (rc != EOS_OK)
        return rc;

    eos_tlv_ctx_t ctx;
    rc = eos_tlv_parse(&ctx, tlv_addr);
    if (rc == EOS_ERR_NOT_FOUND) return EOS_OK;
    if (rc != EOS_OK) return rc;

    eos_tlv_parsed_entry_t entry;
    rc = eos_tlv_find(&ctx, EOS_TLV_MIN_SEC_VER, &entry);
    if (rc == EOS_ERR_NOT_FOUND) return EOS_OK;
    if (rc != EOS_OK) return rc;

    if (entry.len != sizeof(uint32_t)) return EOS_ERR_INVALID;

    uint32_t value = 0;
    rc = eos_tlv_read_data(&ctx, &entry, &value, sizeof(value));
    if (rc != EOS_OK) return rc;

    *counter_out = value;
    return EOS_OK;
}

int eos_rollback_get_device_counter(uint32_t *counter_out)
{
    if (!counter_out) return EOS_ERR_INVALID;
    return eos_hal_monotonic_read(counter_out);
}

int eos_rollback_verify(uint32_t image_counter)
{
    uint32_t floor = 0;
    int rc = eos_hal_monotonic_read(&floor);

    if (rc == EOS_ERR_NOT_SUPPORTED) return EOS_OK;
    if (rc != EOS_OK) return rc;

    if (image_counter < floor) return EOS_ERR_ANTI_ROLLBACK;

    return EOS_OK;
}

void eos_rollback_stage(uint32_t image_counter)
{
    g_staged_counter = image_counter;
    g_staged_valid = true;
}

void eos_rollback_clear_staged(void)
{
    g_staged_counter = 0;
    g_staged_valid = false;
}

int eos_rollback_commit(void)
{
    if (!g_staged_valid) return EOS_OK;

    uint32_t current = 0;
    int rc = eos_hal_monotonic_read(&current);
    if (rc == EOS_ERR_NOT_SUPPORTED) {
        eos_rollback_clear_staged();
        return EOS_ERR_NOT_SUPPORTED;
    }
    if (rc != EOS_OK) return rc;

    if (g_staged_counter <= current) {
        eos_rollback_clear_staged();
        return EOS_OK;
    }

    uint32_t steps = g_staged_counter - current;
    if (steps > EOS_ROLLBACK_MAX_STEP) {
        /* Refuse to burn an implausible number of fuses. */
        return EOS_ERR_INVALID;
    }

    for (uint32_t i = 0; i < steps; i++) {
        rc = eos_hal_monotonic_increment();
        if (rc != EOS_OK) return rc;
    }

    /* A fuse write can fail without reporting an error. */
    uint32_t after = 0;
    rc = eos_hal_monotonic_read(&after);
    if (rc != EOS_OK) return rc;
    if (after < g_staged_counter) return EOS_ERR_GENERIC;

    eos_rollback_clear_staged();
    return EOS_OK;
}
