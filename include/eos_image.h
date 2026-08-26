// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_image.h
 * @brief Image header format for eBootloader
 *
 * Defines the firmware image header structure used for image
 * identification, versioning, integrity checks, and signature
 * verification.
 */

#ifndef EOS_IMAGE_H
#define EOS_IMAGE_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Image Header ---------------- */

typedef struct {
    uint32_t magic;           /* EOS_IMG_MAGIC */
    uint16_t hdr_version;     /* Header format version */
    uint16_t hdr_size;        /* Size of this header in bytes */
    uint32_t image_size;      /* Payload size (excluding header) */
    uint32_t load_addr;       /* Target load address */
    uint32_t entry_addr;      /* Entry point address */
    uint32_t image_version;   /* Firmware version (EOS_VERSION_MAKE) */
    uint32_t flags;           /* EOS_IMG_FLAG_* */
    uint8_t  hash[EOS_HASH_SIZE];   /* SHA-256 hash of payload */
    uint8_t  sig_type;        /* eos_sig_type_t */
    uint8_t  sig_len;         /* Actual signature length */
    uint8_t  reserved[30];    /* Reserved for future use */
    uint8_t  signature[EOS_SIG_MAX_SIZE]; /* Digital signature */
} eos_image_header_t;

#define EOS_IMAGE_HDR_VERSION  1

/* ---------------- Image Validation API ---------------- */

/**
 * @brief Parse and validate an image header at the given address.
 * @param addr  Flash address where the header resides.
 * @param out   Pointer to structure to populate on success.
 * @return EOS_OK on valid header, negative error code otherwise.
 */
int eos_image_parse_header(uint32_t addr, eos_image_header_t *out);

/**
 * @brief Verify image integrity using CRC32 or hash.
 * @param hdr   Parsed image header.
 * @param addr  Flash address of the image payload (after header).
 * @return EOS_OK if integrity check passes, EOS_ERR_CRC on hash or CRC
 *         mismatch, EOS_ERR_FLASH if the payload could not be read.
 */
int eos_image_verify_integrity(const eos_image_header_t *hdr, uint32_t addr);

/**
 * @brief Verify image digital signature.
 * @param hdr   Parsed image header.
 * @return EOS_OK if signature is valid, EOS_ERR_SIGNATURE on failure.
 */
int eos_image_verify_signature(const eos_image_header_t *hdr);

/**
 * @brief Check if an image version is acceptable (anti-rollback).
 * @param candidate_version  Version of the candidate image.
 * @param min_version        Minimum allowed version.
 * @return EOS_OK if version is acceptable, EOS_ERR_VERSION otherwise.
 */
int eos_image_check_version(uint32_t candidate_version, uint32_t min_version);

/**
 * @brief Compute CRC32 over a flash region.
 *
 * Reports read failures separately from the computed value. A CRC returned
 * in-band cannot do this: every 32-bit value is a legal CRC, so there is no
 * value left over to mean "the read failed".
 *
 * @param addr  Start address.
 * @param len   Length in bytes.
 * @param out   Receives the CRC32 value on success; untouched on failure.
 * @return EOS_OK on success, EOS_ERR_FLASH if the region could not be read,
 *         EOS_ERR_INVALID if @p out is NULL.
 */
int eos_crc32(uint32_t addr, size_t len, uint32_t *out);

#ifdef __cplusplus
}
#endif
#endif /* EOS_IMAGE_H */
