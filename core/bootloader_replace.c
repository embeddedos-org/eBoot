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
 * Overflow-safe: the ends are computed in 64 bits. */
static bool ranges_intersect(uint32_t addr, size_t len,
                             uint32_t start, uint32_t size)
{
    if (len == 0 || size == 0)
        return false;
    uint64_t a_end = (uint64_t)addr + len;
    uint64_t b_end = (uint64_t)start + size;
    return (uint64_t)addr < b_end && a_end > (uint64_t)start;
}

/* Remaining bytes from @p image_addr to the end of the flash area that
 * contains it: a slot (staged image) or the stage-1 region (installed
 * image under verification). The image header is stored at the area base
 * on every path that calls this. */
static int image_room_from(uint32_t image_addr, uint32_t *room_out)
{
    static const eos_slot_t slots[] = {
        EOS_SLOT_A, EOS_SLOT_B, EOS_SLOT_RECOVERY
    };
    size_t i;

    if (!room_out)
        return EOS_ERR_INVALID;

    for (i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
        uint32_t base = eos_hal_slot_addr(slots[i]);
        uint32_t size = eos_hal_slot_size(slots[i]);

        if (base == 0 || size == 0)
            continue;
        if (image_addr < base)
            continue;
        if (image_addr - base >= size)
            continue;

        *room_out = size - (image_addr - base);
        return EOS_OK;
    }

    /* Not in a slot: the stage-1 region itself. */
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (ops && ops->bootloader_size != 0) {
        uint32_t base = stage1_base(ops);
        uint32_t end = bootloader_end(ops);
        if (image_addr >= base && image_addr < end) {
            *room_out = end - image_addr;
            return EOS_OK;
        }
    }

    return EOS_ERR_INVALID;
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

    volatile uint8_t diff = 0;
    for (uint32_t i = 0; i < EOS_IMG_TLV_HASH_LEN; i++)
        diff |= (uint8_t)(digest[i] ^ hdr->tlv_hash[i]);

    return (diff == 0) ? EOS_OK : EOS_ERR_INVALID;
}

/* The EOS_TLV_TARGET value must be "<board_id>:stage-1" for this board.
 * The value length is bounded: a board identity longer than 32 bytes
 * cannot be a board this bootloader knows, and the comparison buffer
 * lives on the stack. */
#define BTLDR_TARGET_MAX_BOARD_ID  32
#define BTLDR_TARGET_STAGE_SUFFIX  ":stage-1"

static int target_matches_board(const eos_image_header_t *hdr,
                                uint32_t image_addr,
                                const eos_board_ops_t *ops)
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

    eos_tlv_ctx_t ctx;
    rc = eos_tlv_parse(&ctx, tlv_addr);
    if (rc != EOS_OK)
        return rc;

    eos_tlv_parsed_entry_t entry;
    rc = eos_tlv_find(&ctx, EOS_TLV_TARGET, &entry);
    if (rc != EOS_OK)
        return EOS_ERR_INVALID;  /* no target entry: not a replacement image */

    size_t board_len = strlen(ops->board_id);
    size_t suffix_len = strlen(BTLDR_TARGET_STAGE_SUFFIX);
    if (board_len == 0 || board_len > BTLDR_TARGET_MAX_BOARD_ID)
        return EOS_ERR_NOT_SUPPORTED;
    if (entry.len != board_len + suffix_len)
        return EOS_ERR_INVALID;

    char expected[BTLDR_TARGET_MAX_BOARD_ID +
                  sizeof(BTLDR_TARGET_STAGE_SUFFIX)];
    memcpy(expected, ops->board_id, board_len);
    memcpy(expected + board_len, BTLDR_TARGET_STAGE_SUFFIX, suffix_len);

    char value[64];
    if (entry.len > sizeof(value))
        return EOS_ERR_INVALID;
    rc = eos_tlv_read_data(&ctx, &entry, value, sizeof(value));
    if (rc != EOS_OK)
        return rc;

    /* Constant-time: the value is attacker-influenced and the device
     * offers unlimited retries. */
    volatile uint8_t diff = 0;
    for (uint32_t i = 0; i < entry.len; i++)
        diff |= (uint8_t)(value[i] ^ expected[i]);

    return (diff == 0) ? EOS_OK : EOS_ERR_INVALID;
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

    /* The whole image must fit the area it was staged in, and must fit
     * the stage-1 region it will be copied into. Overflow-checked with
     * the same discipline as rollback.c's slot_remain_from(). */
    uint32_t room = 0;
    rc = image_room_from(image_addr, &room);
    if (rc != EOS_OK)
        return rc;
    if (hdr.hdr_size > room)
        return EOS_ERR_INVALID;
    room -= hdr.hdr_size;
    if (hdr.image_size > room)
        return EOS_ERR_INVALID;
    room -= hdr.image_size;
    if (hdr.tlv_len > room)
        return EOS_ERR_INVALID;
    if ((uint64_t)hdr.hdr_size + hdr.image_size + hdr.tlv_len >
        (uint64_t)(end - base))
        return EOS_ERR_INVALID;

    rc = target_matches_board(&hdr, image_addr, ops);
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

    /* Step 5 -- TLV binding, then anti-rollback. The counter is read
     * through eos_rollback_read_image_counter(), which re-authenticates
     * the TLV area against the now-signature-verified header. */
    uint32_t counter = 0;
    rc = eos_rollback_read_image_counter(image_addr, &counter);
    if (rc != EOS_OK)
        return rc;
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
    memcpy(bctl.replace_intent.image_sha256, hdr.hash,
           sizeof(bctl.replace_intent.image_sha256));
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
