// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file bootloader_replace.c
 * @brief Stage-0 replacement executor (ADR-022)
 *
 * Runs the design note §10 transaction. Stage-0 only: this file is
 * compiled into the stage-0 image (and, for host tests, directly into
 * the test binary), never into stage-1 or the application.
 *
 * What is copied: only the staged payload. The staged image is an
 * eos_image (header + payload + TLV area) in the recovery slot; the
 * stage-1 region holds the raw stage-1 binary, vector table first, with
 * no image header. The copy writes hdr.image_size bytes from
 * staging_addr + hdr.hdr_size to the stage-1 base, and the written bytes
 * are verified by re-hashing the installed region against hdr->hash
 * (§10 step 7). The header and TLV area stay in the staging slot.
 *
 * Fail-closed summary (design note §5): every refusal leaves the device
 * booting the old stage-1 (or in recovery, where noted) and the intent
 * record cleared or left for the next boot. Nothing in this path ever
 * writes the bootloader region speculatively: a stage-1 sector is erased
 * only after the staged image has fully verified in stage-0 on that
 * same boot.
 */

#include "bootloader_replace.h"
#include "eos_bootloader_replace.h"
#include "eos_bootloader_protect.h"
#include "eos_boot_log.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include "eos_rollback.h"
#include <string.h>

/* Copy chunk: matches the recovery transport's write chunk. */
#define EBLDR_REPLACE_COPY_CHUNK  256

extern void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail);

static uint32_t stage1_base(const eos_board_ops_t *ops)
{
    return ops->flash_base + ops->app_vector_offset;
}

static uint32_t stage1_size(const eos_board_ops_t *ops)
{
    return ops->bootloader_addr + ops->bootloader_size - stage1_base(ops);
}

static void log_refusal(void)
{
    eos_boot_log_append(EOS_LOG_BOOT_FAIL, EOS_SLOT_NONE,
                        EBLDR_FAIL_BTLDR_REPLACE);
}

/* Clear the intent record. Best effort: a failed save leaves the record
 * for the next boot, which retries the same disposition -- every path
 * that clears is idempotent. */
static void clear_intent(eos_bootctl_t *bctl)
{
    memset(&bctl->replace_intent, 0, sizeof(bctl->replace_intent));
    (void)eos_bootctl_save(bctl);
}

/* Re-apply the hardware write protection and verify it by read-back.
 * Uses the HAL directly, not eos_bootloader_protect_apply(): the
 * executor re-protects unconditionally after a privileged copy, while
 * the policy layer is for the per-boot call in hw_init_minimal.c (a
 * NEVER-policy build must not leave a copied bootloader unprotected).
 * A board that declares a region but has no protect primitive is refused
 * by the caller's board gates before this is reached. */
static int reapply_protection(void)
{
    int rc = eos_hal_flash_protect_bootloader();
    if (rc != EOS_OK)
        return rc;
    uint32_t status = 0;
    rc = eos_hal_flash_protect_status(&status);
    if (rc != EOS_OK)
        return rc;
    if (status != EOS_BOOTLOADER_PROTECT_PROTECTED)
        return EOS_ERR_FLASH;
    return EOS_OK;
}

/* Does the installed stage-1 payload already equal the staged payload?
 * A byte comparison against the verified staged image: the staged image
 * is trusted at this point (it just passed §3), so equality proves the
 * copy already happened -- the post-copy reset with an uncleared record
 * (§6), or a COPYING save that never landed. */
static bool installed_matches_staged(uint32_t staging_addr,
                                     const eos_image_header_t *hdr)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    uint32_t base = stage1_base(ops);
    uint32_t payload_addr = staging_addr + hdr->hdr_size;
    uint8_t installed[EBLDR_REPLACE_COPY_CHUNK];
    uint8_t staged[EBLDR_REPLACE_COPY_CHUNK];
    uint32_t off = 0;

    while (off < hdr->image_size) {
        uint32_t chunk = hdr->image_size - off;
        if (chunk > sizeof(installed))
            chunk = sizeof(installed);
        if (eos_hal_flash_read(base + off, installed, chunk) != EOS_OK)
            return false;
        if (eos_hal_flash_read(payload_addr + off, staged, chunk) != EOS_OK)
            return false;
        if (memcmp(installed, staged, chunk) != 0)
            return false;
        off += chunk;
    }
    return true;
}

/* The copy itself: mint the token, erase the stage-1 sectors, copy the
 * staged payload, verify the written bytes by re-hash, re-apply WRP,
 * clear the record, reset. Any failure after the token is minted halts:
 * the record stays COPYING so the next boot knows the copy started (§6),
 * and stage-0 must not boot a stage-1 it cannot prove is intact. */
static int copy_and_finish(eos_bootctl_t *bctl,
                           const eos_image_header_t *hdr,
                           uint32_t staging_addr)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    uint32_t base = stage1_base(ops);
    uint32_t size = stage1_size(ops);
    uint32_t payload_addr = staging_addr + hdr->hdr_size;
    int rc;

    /* No application code runs during the copy, and the token window
     * must not be interrupted into other code. Interrupts stay disabled:
     * every exit from here resets or halts. */
    eos_hal_disable_interrupts();
    eos_hal_bootloader_token_mint();

    rc = eos_hal_flash_erase_bootloader(base, size);
    if (rc == EOS_OK) {
        uint8_t buf[EBLDR_REPLACE_COPY_CHUNK];
        uint32_t off = 0;
        while (off < hdr->image_size && rc == EOS_OK) {
            uint32_t chunk = hdr->image_size - off;
            if (chunk > sizeof(buf))
                chunk = sizeof(buf);
            rc = eos_hal_flash_read(payload_addr + off, buf, chunk);
            if (rc == EOS_OK)
                rc = eos_hal_flash_write_bootloader(base + off, buf, chunk);
            off += chunk;
            eos_hal_watchdog_feed();
        }
    }
    eos_hal_bootloader_token_clear();

    if (rc != EOS_OK) {
        log_refusal();
        return EOS_ERR_FLASH;
    }

    /* §10 step 7: re-hash the installed region against the staged
     * image's digest. */
    rc = eos_crypto_verify_image(base, hdr->image_size, hdr->hash);
    if (rc != EOS_OK) {
        log_refusal();
        return EOS_ERR_FLASH;
    }

    rc = reapply_protection();
    if (rc != EOS_OK) {
        log_refusal();
        return EOS_ERR_FLASH;
    }

    clear_intent(bctl);
    eos_hal_system_reset();
    return EBLR_REPLACE_RESET_PENDING;
}

int eblr_bootloader_replace_execute(const eos_bootctl_t *bctl)
{
    if (!bctl)
        return EOS_ERR_INVALID;

    const eos_bootloader_replace_intent_t *intent = &bctl->replace_intent;
    if (intent->magic != EOS_BTLDR_REPLACE_MAGIC)
        return EBLR_REPLACE_PROCEED;

    eos_bootctl_t local = *bctl;

    /* Unknown version or unknown state: refuse, clear, boot the old
     * stage-1. A version this stage-0 does not understand is never
     * parsed optimistically (§8). */
    if (intent->version != EOS_BTLDR_REPLACE_VERSION ||
        (intent->state != EOS_BTLDR_REPLACE_PENDING &&
         intent->state != EOS_BTLDR_REPLACE_COPYING)) {
        log_refusal();
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    /* Board gates: a board with a region but no protect primitive, no
     * unprotect primitive, or no board identity reports
     * EOS_ERR_NOT_SUPPORTED and the replacement is refused -- the same
     * fail-closed signal #191 established, never a silent success. */
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return EOS_ERR_GENERIC;
    if (ops->bootloader_size == 0 || !ops->board_id ||
        !ops->flash_unprotect_bootloader || !ops->flash_protect_bootloader) {
        log_refusal();
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    if (intent->staging_slot != (uint32_t)EOS_SLOT_RECOVERY) {
        log_refusal();
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    uint32_t staging_addr = eos_hal_slot_addr(EOS_SLOT_RECOVERY);
    uint32_t staging_size = eos_hal_slot_size(EOS_SLOT_RECOVERY);
    if (staging_addr == 0 || staging_size == 0) {
        log_refusal();
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    /* The staging slot must not be the bootloader region. The verify
     * below would catch a staged image that does not fit its slot, but
     * a slot that IS the region must never be staged into at all. */
    if (staging_addr >= ops->bootloader_addr &&
        staging_addr < ops->bootloader_addr + ops->bootloader_size) {
        log_refusal();
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    /* §10 step 4: re-verify the staged image in stage-0. Stage-1 is the
     * code under replacement and cannot vouch for its own successor. */
    eos_image_header_t hdr;
    uint32_t counter = 0;
    int rc = eos_bootloader_replace_verify_image(staging_addr, &hdr, &counter);
    if (rc != EOS_OK) {
        log_refusal();
        if (intent->state == EOS_BTLDR_REPLACE_COPYING) {
            /* The copy may have started, so stage-1 may be partial.
             * Never jump to it: recovery (stage-0 intact) re-stages and
             * the transaction repeats. The record stays COPYING. */
            return EOS_ERR_NO_IMAGE;
        }
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    /* Cross-check the record against the verified staged image. The
     * record is written by stage-1, so this proves consistency, not
     * authenticity -- the verification above is what authenticates. */
    if (memcmp(intent->image_sha256, hdr.hash,
               sizeof(intent->image_sha256)) != 0 ||
        intent->image_counter != counter ||
        intent->image_size != hdr.hdr_size + hdr.image_size + hdr.tlv_len) {
        log_refusal();
        if (intent->state == EOS_BTLDR_REPLACE_COPYING)
            return EOS_ERR_NO_IMAGE;
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    /* The copy already happened (post-copy reset with an uncleared
     * record, or a COPYING save that never landed): re-apply WRP,
     * clear, and boot the new stage-1. */
    if (installed_matches_staged(staging_addr, &hdr)) {
        rc = reapply_protection();
        if (rc != EOS_OK) {
            log_refusal();
            return EOS_ERR_FLASH;
        }
        clear_intent(&local);
        return EBLR_REPLACE_PROCEED;
    }

    if (intent->state == EOS_BTLDR_REPLACE_PENDING) {
        /* §10 step 5: lift WRP, record COPYING, and reset on boards
         * whose option bytes reload on reset. Until this reset
         * completes, the region is still hardware-protected. */
        rc = eos_hal_flash_unprotect_bootloader();
        if (rc == EOS_ERR_NOT_SUPPORTED) {
            log_refusal();
            clear_intent(&local);
            return EBLR_REPLACE_PROCEED;
        }
        if (rc != EOS_OK) {
            /* Halt, consistent with stage0/hw_init_minimal.c: booting
             * while believing itself protected is the fail-open shape
             * #191 exists to remove. The record stays PENDING. */
            log_refusal();
            return EOS_ERR_FLASH;
        }

        local.replace_intent.state = EOS_BTLDR_REPLACE_COPYING;
        if (eos_bootctl_save(&local) != EOS_OK) {
            /* The record stays PENDING in flash. The copy below still
             * runs in this boot, and the next boot's PENDING flow
             * re-verifies and repeats it idempotently. */
            log_refusal();
        }

        if (ops->bootloader_protect_needs_reset) {
            eos_hal_system_reset();
            return EBLR_REPLACE_RESET_PENDING;
        }
        /* Protection lifts immediately on this board: copy in this boot. */
    }

    /* COPYING on entry, or PENDING with immediate-effect protection:
     * the staged image verified on this boot, so the copy proceeds. */
    return copy_and_finish(&local, &hdr, staging_addr);
}
