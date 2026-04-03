// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file image_tlv.c
 * @brief TLV (Type-Length-Value) metadata parser for firmware images
 *
 * mcuboot-inspired TLV area parser. TLVs provide extensible metadata
 * such as key hashes, security versions, nonces, and dependencies.
 */

#include "eos_image_tlv.h"
#include "eos_hal.h"
#include <string.h>

int eos_tlv_parse(eos_tlv_ctx_t *ctx, uint32_t tlv_addr)
{
    if (!ctx) return EOS_ERR_INVALID;

    memset(ctx, 0, sizeof(*ctx));
    ctx->base_addr = tlv_addr;

    /* Read TLV info header */
    eos_tlv_info_t info;
    int rc = eos_hal_flash_read(tlv_addr, &info, sizeof(info));
    if (rc != EOS_OK) return EOS_ERR_FLASH;

    if (info.magic != EOS_TLV_INFO_MAGIC) {
        /* No TLV area — not an error, just no TLVs */
        return EOS_ERR_NOT_FOUND;
    }

    if (info.tlv_total_len < sizeof(eos_tlv_info_t) ||
        info.tlv_total_len > EOS_TLV_MAX_SIZE) {
        return EOS_ERR_INVALID;
    }

    ctx->total_len = info.tlv_total_len;

    /* Parse individual TLV entries */
    uint32_t offset = sizeof(eos_tlv_info_t);
    while (offset + sizeof(eos_tlv_entry_hdr_t) <= info.tlv_total_len &&
           ctx->count < EOS_TLV_MAX_ENTRIES) {

        eos_tlv_entry_hdr_t entry_hdr;
        rc = eos_hal_flash_read(tlv_addr + offset, &entry_hdr, sizeof(entry_hdr));
        if (rc != EOS_OK) break;

        if (entry_hdr.len == 0 && entry_hdr.type == 0) break; /* end sentinel */

        if (offset + sizeof(eos_tlv_entry_hdr_t) + entry_hdr.len > info.tlv_total_len) {
            break; /* truncated entry */
        }

        ctx->entries[ctx->count].type = entry_hdr.type;
        ctx->entries[ctx->count].len = entry_hdr.len;
        ctx->entries[ctx->count].offset = tlv_addr + offset + sizeof(eos_tlv_entry_hdr_t);
        ctx->count++;

        offset += sizeof(eos_tlv_entry_hdr_t) + entry_hdr.len;
        /* Align to 4-byte boundary */
        offset = (offset + 3) & ~3u;
    }

    return EOS_OK;
}

int eos_tlv_find(const eos_tlv_ctx_t *ctx, uint16_t type,
                 eos_tlv_parsed_entry_t *out)
{
    if (!ctx || !out) return EOS_ERR_INVALID;

    for (uint32_t i = 0; i < ctx->count; i++) {
        if (ctx->entries[i].type == type) {
            *out = ctx->entries[i];
            return EOS_OK;
        }
    }

    return EOS_ERR_NOT_FOUND;
}

int eos_tlv_read_data(const eos_tlv_ctx_t *ctx,
                      const eos_tlv_parsed_entry_t *entry,
                      void *buf, size_t buf_sz)
{
    if (!ctx || !entry || !buf) return EOS_ERR_INVALID;
    if (buf_sz < entry->len) return EOS_ERR_FULL;

    return eos_hal_flash_read(entry->offset, buf, entry->len);
}
