// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_image_tlv.h
 * @brief TLV (Type-Length-Value) metadata for firmware images
 *
 * mcuboot-inspired TLV area appended after the fixed image header.
 * TLVs are covered by the image signature for tamper protection.
 *
 * Layout:
 *   [image_header][TLV area][payload]
 *   TLV area = [tlv_info][tlv_entry_0][tlv_entry_1]...
 */

#ifndef EOS_IMAGE_TLV_H
#define EOS_IMAGE_TLV_H

#include "eos_types.h"
#include "eos_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TLV magic for the TLV info header */
#define EOS_TLV_INFO_MAGIC   0x6907

/* TLV types (mcuboot-compatible numbering where applicable) */
#define EOS_TLV_KEYHASH      0x01
#define EOS_TLV_SHA256       0x10
#define EOS_TLV_ED25519      0x24
#define EOS_TLV_DEPENDENCY   0x40
#define EOS_TLV_MIN_SEC_VER  0x50
#define EOS_TLV_NONCE        0x51
#define EOS_TLV_ENC_AES256   0x60

#define EOS_TLV_MAX_ENTRIES  16
#define EOS_TLV_MAX_SIZE     512

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

typedef struct {
    uint16_t magic;
    uint16_t tlv_total_len;
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((packed))
#endif
eos_tlv_info_t;

typedef struct {
    uint16_t type;
    uint16_t len;
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((packed))
#endif
eos_tlv_entry_hdr_t;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

typedef struct {
    uint16_t type;
    uint16_t len;
    uint32_t offset;
} eos_tlv_parsed_entry_t;

typedef struct {
    eos_tlv_parsed_entry_t entries[EOS_TLV_MAX_ENTRIES];
    uint32_t               count;
    uint32_t               base_addr;
    uint16_t               total_len;
} eos_tlv_ctx_t;

/**
 * @brief Parse TLV area from flash at given address.
 * @param ctx       TLV context (caller-allocated).
 * @param tlv_addr  Flash address of the TLV info header.
 * @return EOS_OK on success, EOS_ERR_INVALID if magic mismatch.
 */
int eos_tlv_parse(eos_tlv_ctx_t *ctx, uint32_t tlv_addr);

/**
 * @brief Find a TLV entry by type.
 * @param ctx   Parsed TLV context.
 * @param type  TLV type to find.
 * @param out   Receives the parsed entry.
 * @return EOS_OK if found, EOS_ERR_NOT_FOUND otherwise.
 */
int eos_tlv_find(const eos_tlv_ctx_t *ctx, uint16_t type,
                 eos_tlv_parsed_entry_t *out);

/**
 * @brief Read TLV entry data from flash.
 * @param ctx    TLV context.
 * @param entry  Parsed entry to read.
 * @param buf    Output buffer.
 * @param buf_sz Buffer size.
 * @return EOS_OK on success.
 */
int eos_tlv_read_data(const eos_tlv_ctx_t *ctx,
                      const eos_tlv_parsed_entry_t *entry,
                      void *buf, size_t buf_sz);

#ifdef __cplusplus
}
#endif
#endif /* EOS_IMAGE_TLV_H */
