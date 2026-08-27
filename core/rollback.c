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
#include "eos_hal.h"
#include <string.h>

static uint32_t g_staged_counter;
static bool     g_staged_valid;

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
