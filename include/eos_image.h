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

/**
 * @brief Bytes of the TLV area's SHA-256 digest carried in the header.
 *
 * The TLV area sits after the payload, so neither the signature (which covers
 * the header prefix) nor hash[] (which covers exactly image_size payload bytes)
 * reaches it. tlv_len and tlv_hash do, and they are inside the signed prefix —
 * that is what makes a TLV-declared value such as EOS_TLV_MIN_SEC_VER
 * trustworthy enough to gate anti-rollback on.
 *
 * The digest is truncated to 224 bits so the pair fits the 30 bytes previously
 * reserved between sig_len and signature[], leaving every other field at the
 * offset the signing tools already address. Second-preimage resistance at 224
 * bits is far beyond what an attacker rewriting a ~64-byte TLV blob can reach.
 */
#define EOS_IMG_TLV_HASH_LEN  28

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
    uint16_t tlv_len;         /* Bytes of TLV area following the payload; 0 = none */
    uint8_t  tlv_hash[EOS_IMG_TLV_HASH_LEN]; /* Truncated SHA-256 of that area */
    uint8_t  signature[EOS_SIG_MAX_SIZE]; /* Digital signature */
} eos_image_header_t;

/* Header format version.
 *
 * v1 — the digital signature covered hash[] only.
 * v2 — the signature covers the whole header prefix (see EOS_IMG_SIGNED_LEN).
 *      v1 images must be re-signed; their signatures do not verify under v2.
 */
#define EOS_IMAGE_HDR_VERSION  2

/**
 * @brief Length of the header prefix covered by the digital signature.
 *
 * Everything except the signature field itself: magic, hdr_version, hdr_size,
 * image_size, load_addr, entry_addr, image_version, flags, hash, sig_type,
 * sig_len, tlv_len and tlv_hash.
 *
 * Signing hash[] alone leaves every other field unauthenticated. An attacker
 * could keep a legitimately signed image's signature and still change the load
 * address, move the entry point, or clear EOS_IMG_FLAG_HASH_SHA256 to downgrade
 * integrity checking from SHA-256 to forgeable CRC32. hash[] sits inside this
 * prefix, so the payload remains covered transitively.
 */
#define EOS_IMG_SIGNED_LEN  offsetof(eos_image_header_t, signature)

/* The signing tools address these fields by absolute byte offset, so the layout
 * is part of the on-disk format and must not drift. */
#if defined(__cplusplus)
#  define EOS_IMG_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#  define EOS_IMG_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#  define EOS_IMG_STATIC_ASSERT(cond, msg) /* unavailable before C11 */
#endif

EOS_IMG_STATIC_ASSERT(sizeof(eos_image_header_t) == 156,
                      "image header is 156 bytes on the wire");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, hash) == 28,
                      "hash[] must stay at offset 28");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, sig_type) == 60,
                      "sig_type must stay at offset 60");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, tlv_len) == 62,
                      "tlv_len must occupy the first 2 of the 30 formerly "
                      "reserved bytes");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, tlv_hash) == 64,
                      "tlv_hash must occupy the remaining 28 reserved bytes");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, signature) == 92,
                      "signature[] must stay at offset 92");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, tlv_hash) +
                      EOS_IMG_TLV_HASH_LEN ==
                      offsetof(eos_image_header_t, signature),
                      "the TLV binding must be inside the signed prefix");

/* Every remaining field, pinned.
 *
 * Four of the fourteen fields were asserted. Transposing two adjacent
 * same-width fields moves neither sizeof nor any of those four offsets, so it
 * compiled clean: with load_addr and entry_addr swapped, all four existing
 * asserts still passed and the bootloader would load an image at its entry
 * point and jump to its load address. A wire format needs every field pinned,
 * not a representative sample. */
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, magic) == 0,
                      "magic must stay at offset 0");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, hdr_version) == 4,
                      "hdr_version must stay at offset 4");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, hdr_size) == 6,
                      "hdr_size must stay at offset 6");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, image_size) == 8,
                      "image_size must stay at offset 8");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, load_addr) == 12,
                      "load_addr must stay at offset 12");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, entry_addr) == 16,
                      "entry_addr must stay at offset 16");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, image_version) == 20,
                      "image_version must stay at offset 20");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, flags) == 24,
                      "flags must stay at offset 24");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, sig_len) == 61,
                      "sig_len must stay at offset 61");
EOS_IMG_STATIC_ASSERT(offsetof(eos_image_header_t, reserved) == 62,
                      "reserved[] must stay at offset 62");

/* Field widths. An offset assert cannot see a field growing into padding that
 * happens to keep every later offset -- reserved[] absorbs exactly that. */
EOS_IMG_STATIC_ASSERT(sizeof(((eos_image_header_t *)0)->hash) == 32,
                      "hash[] is 32 bytes on the wire");
EOS_IMG_STATIC_ASSERT(sizeof(((eos_image_header_t *)0)->reserved) == 30,
                      "reserved[] is 30 bytes on the wire");
EOS_IMG_STATIC_ASSERT(sizeof(((eos_image_header_t *)0)->signature) == 64,
                      "signature[] is 64 bytes on the wire");

/* Constant values. These travel inside the image, so they are wire format too
 * and an offset assert says nothing about them. Changing EOS_SIG_ED25519 from
 * 3 to 4, or reordering the enum, compiled cleanly and passed every assert
 * above while making eBoot misread the signature type of every image already
 * in the field. eFirmware pins the same numbers on its side
 * (EFW_IMAGE_MAGIC == 0x454F5349u, EFW_SIG_ED25519 == 3); these are the
 * matching half, so the two definitions can no longer drift apart in silence. */
EOS_IMG_STATIC_ASSERT(EOS_IMG_MAGIC == 0x454F5349,
                      "magic is \"EOSI\" and eFirmware stamps the same value");
EOS_IMG_STATIC_ASSERT(EOS_HASH_SIZE == 32, "hash is SHA-256, 32 bytes");
EOS_IMG_STATIC_ASSERT(EOS_SIG_MAX_SIZE == 64, "signature area is 64 bytes");
EOS_IMG_STATIC_ASSERT(EOS_IMG_SIGNED_LEN == 92,
                      "the signed prefix is the 92 bytes before signature[]");
EOS_IMG_STATIC_ASSERT(EOS_IMAGE_HDR_VERSION == 2,
                      "eBoot writes and expects header format v2");
EOS_IMG_STATIC_ASSERT((int)EOS_SIG_NONE == 0, "EOS_SIG_NONE is 0 on the wire");
EOS_IMG_STATIC_ASSERT((int)EOS_SIG_CRC32 == 1, "EOS_SIG_CRC32 is 1 on the wire");
EOS_IMG_STATIC_ASSERT((int)EOS_SIG_SHA256 == 2, "EOS_SIG_SHA256 is 2 on the wire");
EOS_IMG_STATIC_ASSERT((int)EOS_SIG_ED25519 == 3, "EOS_SIG_ED25519 is 3 on the wire");
EOS_IMG_STATIC_ASSERT((int)EOS_SIG_ECDSA == 4, "EOS_SIG_ECDSA is 4 on the wire");

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
 * @param addr  Flash address of the image (header base). The payload starts at
 *              addr + hdr_size; any TLV area follows it, not precedes it.
 * @return EOS_OK if integrity check passes, EOS_ERR_CRC on failure.
 */
int eos_image_verify_integrity(const eos_image_header_t *hdr, uint32_t addr);

/**
 * @brief Verify the image's digital signature.
 *
 * The signature is checked over the first EOS_IMG_SIGNED_LEN bytes of the
 * header — all metadata plus the payload hash — not over hash[] alone.
 *
 * @param hdr   Parsed image header.
 * @return EOS_OK if signature is valid, EOS_ERR_SIGNATURE on failure,
 *         EOS_ERR_KEY if no usable verification key is available.
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
 * @brief Compute CRC32 over a flash region, reporting read failures.
 *
 * Preferred over eos_crc32() anywhere the result is used to decide whether an
 * image is intact: a flash read failure is returned as an error instead of
 * being folded into the CRC value.
 *
 * @param addr     Start address.
 * @param len      Length in bytes.
 * @param out_crc  Receives the CRC32. Untouched unless EOS_OK is returned.
 * @return EOS_OK on success, EOS_ERR_FLASH if the region could not be read,
 *         EOS_ERR_INVALID if @p out_crc is NULL.
 */
int eos_crc32_checked(uint32_t addr, size_t len, uint32_t *out_crc);

/**
 * @brief Compute CRC32 over a memory region.
 *
 * @warning This form cannot report a flash read failure — it returns 0, which
 * is indistinguishable from a region that genuinely hashes to 0. Do not use it
 * to decide whether an image is intact; use eos_crc32_checked() instead.
 * Retained for API compatibility.
 *
 * @param addr  Start address.
 * @param len   Length in bytes.
 * @return CRC32 value, or 0 if the region could not be read.
 */
uint32_t eos_crc32(uint32_t addr, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* EOS_IMAGE_H */
