// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file bootloader_replace.c
 * @brief Privileged in-band stage-1 replacement: request side + shared verification
 *
 * Implements the request side of docs/design/bootloader-replacement-entry.md
 * (ADR-022) and the §3 verification the request side and the stage-0
 * executor share.
 *
 * What is copied, exactly: the staged image is an eos_image
 * (156-byte header v2, payload, TLV area) sitting in the recovery slot.
 * The stage-1 region holds the raw stage-1 binary -- vector table first,
 * no image header -- exactly like the stage-1 the build produces today.
 * The copy therefore writes only the payload (hdr.image_size bytes from
 * staging_addr + hdr.hdr_size) to the stage-1 base. That is also what
 * makes the note's §10 step-7 check well-defined: the stage-1 region
 * re-hashes to hdr->hash, the staged image's digest, only if the region
 * holds the payload and nothing else.
 *
 * The commit hook (eos_bootloader_replace_commit_counter()) reads the new
 * stage-1's counter from the staged image in the recovery slot, not from
 * the installed region (which carries no header) and not from the intent
 * record (which the executor clears before the post-copy reset, §10 step
 * 9). It commits only when the staged image fully verifies AND its
 * payload digest matches the installed stage-1 -- proving this stage-1
 * is the image that was installed -- and only after stage-1 has
 * completed a full boot decision. The signature check in the verify step
 * is load-bearing here: without it, a forged staged image with a large
 * counter could raise the device floor.
 */

#include "eos_bootloader_replace.h"
#include "eos_bootctl.h"
#include "eos_image_tlv.h"
#include "eos_rollback.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include <string.h>

/* ---- Geometry helpers ---- */

/* Stage-1 base: the board's stage-1 vector address. */
static uint32_t stage1_base(const eos_board_ops_t *ops)
{
    return ops->flash_base + ops->app_vector_offset;
}

/* End of the bootloader region (stage-0 + stage-1). */
static uint32_t bootloader_end(const eos_board_ops_t *ops)
{
    return ops->bootloader_addr + ops->bootloader_size;
}

/* Do [addr, addr + len) and [start, start + size) intersect?
 * Overflow-safe: the ends are computed in 64 bits. Used by the
 * request side (stage-1), not by the stage-0 verification path. */
static bool ranges_intersect(uint32_t addr, size_t len,
                             uint32_t start, uint32_t size)
{
    if (len == 0 || size == 0)
        return false;
    uint64_t a_end = (uint64_t)addr + len;
    uint64_t b_end = (uint64_t)start + size;
    return (uint64_t)addr < b_end && a_end > (uint64_t)start;
}

/* ---- TLV target check (design note §3 step 2) ---- */

/* Hash the TLV area and compare against hdr->tlv_hash. The area is
 * authenticated against the header's claim before its value is trusted;
 * the header's claim itself is authenticated later, by the signature
 * step -- so this check can only refuse early, never accept a forgery.
 * Mirrors the binding in core/rollback.c (tlv_area_matches_header);
 * the two cannot share the helper without exposing a static, and the
 * replacement path must not trust rollback.c's counter read for the
 * target value anyway. */
static int tlv_area_matches_header(const eos_image_header_t *hdr,
                                   uint32_t tlv_addr)
{
    eos_sha256_ctx_t sha;
    uint8_t area[EOS_TLV_MAX_SIZE];
    uint8_t digest[EOS_SHA256_DIGEST_SIZE];

    /* One read: the area is at most EOS_TLV_MAX_SIZE (512) bytes, and
     * stage-0 has a 128 KiB RAM to hold it. A chunked read loop costs
     * ~25 bytes more of stage-0 .text for no benefit here. */
    if (eos_hal_flash_read(tlv_addr, area, hdr->tlv_len) != EOS_OK)
        return EOS_ERR_FLASH;
    eos_sha256_init(&sha);
    eos_sha256_update(&sha, area, hdr->tlv_len);
    eos_sha256_final(&sha, digest);

    volatile uint8_t diff = 0;
    for (uint32_t i = 0; i < EOS_IMG_TLV_HASH_LEN; i++)
        diff |= (uint8_t)(digest[i] ^ hdr->tlv_hash[i]);

    return (diff == 0) ? EOS_OK : EOS_ERR_INVALID;
}

/* The EOS_TLV_TARGET value must be "<board_id>:stage-1" for this board.
 * The value length is bounded: a board identity longer than 32 bytes
 * cannot be a board this bootloader knows, and the comparison buffer
 * lives on the stack.
 *
 * The rollback counter comes out of the same authenticated scan:
 * EOS_TLV_MIN_SEC_VER is read here (a missing entry is no claim,
 * counter 0 -- the same reading core/rollback.c gives it), so the
 * verification does one TLV pass instead of re-parsing through
 * eos_rollback_read_image_counter(). That matters for stage-0: the
 * rollback counter reader is otherwise unreferenced there, and
 * pulling it in costs 368 bytes of a 16 KiB budget. The scan below
 * is hand-rolled for the same reason: the shared TLV parser is
 * otherwise unreferenced in stage-0, and linking it costs another
 * 314 bytes. */
#define BTLDR_TARGET_MAX_BOARD_ID  32
#define BTLDR_TARGET_STAGE_SUFFIX  ":stage-1"

/* The board identity length, scanned here instead of strlen: the
 * stage-0 link would otherwise pull libc strlen (216 bytes) for one
 * comparison. Bounded at 33 reads; a longer id is refused below. */
static size_t board_id_len(const char *board_id)
{
    size_t len = 0;
    while (len <= BTLDR_TARGET_MAX_BOARD_ID && board_id[len] != '\0')
        len++;
    return len;
}

/* Scan the TLV area once for the TARGET entry and the MIN_SEC_VER
 * counter. This is a minimal, single-purpose scan -- not the shared
 * eos_tlv_parse -- because stage-0 is a 16 KiB budget and the shared
 * parser (with find and read_data, 314 bytes together) is otherwise
 * unreferenced there: the only other TLV user, core/rollback.c,
 * reaches it through eos_rollback_read_image_counter, which stage-0
 * does not link. The scan mirrors the parser's validation (magic,
 * total length, entry bounds, 4-byte alignment, end sentinel), and
 * stops once both entries are found. It does not mirror the parser's
 * entry cap: there is no context to fill, and the loop is bounded by
 * the area length (at most 512 bytes) either way.
 *
 * One tightening over the shared parser: the info header's total
 * length must equal hdr->tlv_len, the authenticated length. The
 * parser would scan up to the info header's length even past the
 * hashed region; here the scan never reads past the bytes the area
 * hash authenticated. */
static int tlv_scan_target(uint32_t tlv_addr, uint32_t tlv_len,
                           uint32_t *target_off_out,
                           uint32_t *target_len_out,
                           uint32_t *counter_out)
{
    eos_tlv_info_t info;
    int rc = eos_hal_flash_read(tlv_addr, &info, sizeof(info));
    if (rc != EOS_OK)
        return EOS_ERR_FLASH;
    if (info.magic != EOS_TLV_INFO_MAGIC)
        return EOS_ERR_NOT_FOUND;
    /* The total length must equal the header's tlv_len, which the
     * caller already bounded (4..EOS_TLV_MAX_SIZE); the equality
     * subsumes a separate range check here. */
    if (info.tlv_total_len != tlv_len)
        return EOS_ERR_INVALID;

    *target_off_out = 0;
    *target_len_out = 0;
    *counter_out = 0;  /* no MIN_SEC_VER entry: no counter claim */
    bool have_target = false;
    bool have_counter = false;

    uint32_t offset = sizeof(eos_tlv_info_t);
    while (offset + sizeof(eos_tlv_entry_hdr_t) <= info.tlv_total_len) {
        eos_tlv_entry_hdr_t entry_hdr;
        rc = eos_hal_flash_read(tlv_addr + offset, &entry_hdr,
                                sizeof(entry_hdr));
        if (rc != EOS_OK)
            return EOS_ERR_FLASH;
        if (entry_hdr.len == 0 && entry_hdr.type == 0)
            break; /* end sentinel */
        if (offset + sizeof(eos_tlv_entry_hdr_t) + entry_hdr.len >
            info.tlv_total_len)
            break; /* truncated entry */

        if (entry_hdr.type == EOS_TLV_TARGET && !have_target) {
            *target_off_out = tlv_addr + offset +
                sizeof(eos_tlv_entry_hdr_t);
            *target_len_out = entry_hdr.len;
            have_target = true;
        } else if (entry_hdr.type == EOS_TLV_MIN_SEC_VER && !have_counter) {
            /* A present counter entry must be a uint32_t,
             * little-endian -- the same reading core/rollback.c
             * gives it. */
            if (entry_hdr.len != sizeof(uint32_t))
                return EOS_ERR_INVALID;
            rc = eos_hal_flash_read(tlv_addr + offset +
                                        sizeof(eos_tlv_entry_hdr_t),
                                    counter_out, sizeof(uint32_t));
            if (rc != EOS_OK)
                return EOS_ERR_FLASH;
            have_counter = true;
        }
        if (have_target && have_counter)
            break; /* both entries found */
        offset += sizeof(eos_tlv_entry_hdr_t) + entry_hdr.len;
        offset = (offset + 3) & ~3u; /* align to 4-byte boundary */
    }
    if (!have_target)
        return EOS_ERR_INVALID;  /* no target entry: not a replacement image */
    return EOS_OK;
}

static int target_matches_board(const eos_image_header_t *hdr,
                                uint32_t image_addr,
                                const eos_board_ops_t *ops,
                                uint32_t *counter_out)
{
    if (hdr->tlv_len == 0)
        return EOS_ERR_INVALID;
    if (hdr->tlv_len < sizeof(eos_tlv_info_t) ||
        hdr->tlv_len > EOS_TLV_MAX_SIZE)
        return EOS_ERR_INVALID;

    /* The TLV area follows the header and payload. */
    uint32_t tlv_addr = image_addr;
    if (tlv_addr + hdr->hdr_size < tlv_addr)
        return EOS_ERR_INVALID;
    tlv_addr += hdr->hdr_size;
    if (tlv_addr + hdr->image_size < tlv_addr)
        return EOS_ERR_INVALID;
    tlv_addr += hdr->image_size;

    int rc = tlv_area_matches_header(hdr, tlv_addr);
    if (rc != EOS_OK)
        return rc;

    /* One scan finds the target entry and the counter, from the
     * area the hash just authenticated. */
    uint32_t target_off = 0;
    uint32_t target_len = 0;
    rc = tlv_scan_target(tlv_addr, hdr->tlv_len, &target_off,
                         &target_len, counter_out);
    if (rc != EOS_OK)
        return rc;

    size_t board_len = board_id_len(ops->board_id);
    size_t suffix_len = sizeof(BTLDR_TARGET_STAGE_SUFFIX) - 1;
    if (board_len == 0 || board_len > BTLDR_TARGET_MAX_BOARD_ID)
        return EOS_ERR_NOT_SUPPORTED;
    if (target_len != board_len + suffix_len)
        return EOS_ERR_INVALID;

    char value[64];
    if (target_len > sizeof(value))
        return EOS_ERR_INVALID;
    rc = eos_hal_flash_read(target_off, value, target_len);
    if (rc != EOS_OK)
        return rc;

    /* Constant-time: the value is attacker-influenced and the device
     * offers unlimited retries. Compared directly against the board
     * id and the ":stage-1" suffix, byte by byte -- no expected
     * buffer is built, so no memcpy is needed here (and no fortified
     * memcpy is risked). */
    volatile uint8_t diff = 0;
    for (uint32_t i = 0; i < target_len; i++) {
        uint8_t want = (i < board_len)
            ? (uint8_t)ops->board_id[i]
            : (uint8_t)BTLDR_TARGET_STAGE_SUFFIX[i - board_len];
        diff |= (uint8_t)(value[i] ^ want);
    }
    if (diff != 0)
        return EOS_ERR_INVALID;

    return EOS_OK;
}

/* ---- Shared verification (design note §3) ---- */

int eos_bootloader_replace_verify_image(uint32_t image_addr,
                                        eos_image_header_t *hdr_out,
                                        uint32_t *counter_out)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return EOS_ERR_GENERIC;
    if (ops->bootloader_size == 0)
        return EOS_ERR_NOT_SUPPORTED;
    if (!ops->board_id)
        return EOS_ERR_NOT_SUPPORTED;

    eos_image_header_t hdr;
    int rc = eos_image_parse_header(image_addr, &hdr);
    if (rc != EOS_OK)
        return rc;

    /* Step 2 -- target identity, before any signature work is spent. */
    uint32_t base = stage1_base(ops);
    uint32_t end = bootloader_end(ops);
    if (base < ops->bootloader_addr || base >= end)
        return EOS_ERR_NOT_SUPPORTED;
    if (hdr.load_addr != base)
        return EOS_ERR_INVALID;
    if (hdr.entry_addr < base || hdr.entry_addr >= end)
        return EOS_ERR_INVALID;

    /* The whole image must fit the stage-1 region it will be copied
     * into. No separate "fits in its staging slot" check: every caller
     * stages at a slot base, the total here is bounded by the stage-1
     * region (48 KiB on STM32F4) which is smaller than any slot, and
     * the integrity + signature steps below authenticate the bytes
     * read, wherever they sit. tlv_len is bounded first (EOS_TLV_MAX_
     * SIZE, 512) so the sum cannot overflow 32 bits: hdr_size <= 4096
     * and image_size <= 16 MiB are enforced by the header parse. */
    if (hdr.tlv_len > EOS_TLV_MAX_SIZE)
        return EOS_ERR_INVALID;
    if (hdr.hdr_size + hdr.image_size + hdr.tlv_len > end - base)
        return EOS_ERR_INVALID;

    uint32_t counter = 0;
    rc = target_matches_board(&hdr, image_addr, ops, &counter);
    if (rc != EOS_OK)
        return rc;

    /* Step 3 -- integrity. SHA-256 is required for a bootloader
     * replacement: a CRC-only image is refused, not downgraded. */
    if (!(hdr.flags & EOS_IMG_FLAG_HASH_SHA256))
        return EOS_ERR_INVALID;
    rc = eos_image_verify_integrity(&hdr, image_addr);
    if (rc != EOS_OK)
        return rc;

    /* Step 4 -- signature, through the single policy point. */
    rc = eos_image_verify_signature(&hdr);
    if (rc != EOS_OK)
        return rc;

    /* Step 5 -- anti-rollback. The counter came from the target
     * check's authenticated TLV parse (step 2), after the header's
     * signature was verified in step 4, so no re-read is needed. */
    rc = eos_rollback_verify(counter);
    if (rc != EOS_OK)
        return rc;

    if (hdr_out)
        *hdr_out = hdr;
    if (counter_out)
        *counter_out = counter;
    return EOS_OK;
}

/* ---- Request side (design note §9.2) ---- */

int eos_bootloader_replace_request(eos_slot_t staging_slot)
{
    if (staging_slot != EOS_SLOT_RECOVERY)
        return EOS_ERR_INVALID;

    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return EOS_ERR_GENERIC;
    if (ops->bootloader_size == 0 || !ops->board_id ||
        !ops->flash_unprotect_bootloader)
        return EOS_ERR_NOT_SUPPORTED;

    uint32_t staging_addr = eos_hal_slot_addr(staging_slot);
    uint32_t staging_size = eos_hal_slot_size(staging_slot);
    if (staging_addr == 0 || staging_size == 0)
        return EOS_ERR_NOT_SUPPORTED;
    if (ranges_intersect(staging_addr, staging_size,
                         ops->bootloader_addr, ops->bootloader_size))
        return EOS_ERR_INVALID;

    eos_bootctl_t bctl;
    (void)eos_bootctl_load(&bctl);  /* defaults on corrupt copies; the intent is IDLE there */
    if (bctl.replace_intent.magic == EOS_BTLDR_REPLACE_MAGIC &&
        bctl.replace_intent.state != EOS_BTLDR_REPLACE_IDLE)
        return EOS_ERR_BUSY;

    eos_image_header_t hdr;
    uint32_t counter = 0;
    int rc = eos_bootloader_replace_verify_image(staging_addr, &hdr, &counter);
    if (rc != EOS_OK)
        return rc;

    memset(&bctl.replace_intent, 0, sizeof(bctl.replace_intent));
    bctl.replace_intent.magic = EOS_BTLDR_REPLACE_MAGIC;
    bctl.replace_intent.version = EOS_BTLDR_REPLACE_VERSION;
    bctl.replace_intent.state = EOS_BTLDR_REPLACE_PENDING;
    bctl.replace_intent.staging_slot = (uint32_t)staging_slot;
    /* The digest is copied byte by byte, not with memcpy: a memcpy of
     * a constant 32 bytes into a stack struct is fortified to
     * __memcpy_chk by some toolchains (Ubuntu's gcc 10.3.1), and
     * __memcpy_chk pulls __chk_fail -> raise -> signal -> malloc ->
     * _sbrk into the firmware link, which has no heap (undefined
     * `end`). A loop keeps the link heap-free. */
    for (uint32_t i = 0; i < sizeof(bctl.replace_intent.image_sha256); i++)
        bctl.replace_intent.image_sha256[i] = hdr.hash[i];
    bctl.replace_intent.image_counter = counter;
    bctl.replace_intent.image_size =
        hdr.hdr_size + hdr.image_size + hdr.tlv_len;

    rc = eos_bootctl_save(&bctl);
    if (rc != EOS_OK)
        return rc;

    /* Stage-0 performs the replacement on the next boot. This call does
     * not return on a device. */
    eos_hal_system_reset();
    return EOS_OK;
}

/* ---- Counter commit (design note §4, §10 step 10) ---- */

int eos_bootloader_replace_commit_counter(void)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return EOS_ERR_GENERIC;
    if (ops->bootloader_size == 0)
        return EOS_ERR_NOT_SUPPORTED;

    uint32_t base = stage1_base(ops);
    uint32_t staging_addr = eos_hal_slot_addr(EOS_SLOT_RECOVERY);
    if (staging_addr == 0)
        return EOS_ERR_NOT_SUPPORTED;

    /* The staged image must fully verify -- including its signature, so
     * a forged staged image cannot raise the floor -- and the installed
     * stage-1 must be that image: its payload digest must match the
     * staged image's digest. An unrelated image in the recovery slot
     * (the golden recovery image, a future candidate) fails one of the
     * two checks and commits nothing. */
    eos_image_header_t hdr;
    uint32_t counter = 0;
    int rc = eos_bootloader_replace_verify_image(staging_addr, &hdr, &counter);
    if (rc != EOS_OK)
        return rc;

    rc = eos_crypto_verify_image(base, hdr.image_size, hdr.hash);
    if (rc != EOS_OK)
        return EOS_ERR_NO_IMAGE;

    eos_rollback_stage(counter);
    return eos_rollback_commit();
}
