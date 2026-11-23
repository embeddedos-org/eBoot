// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_bootloader_replace.h
 * @brief Privileged in-band stage-1 replacement entry point (ADR-022)
 *
 * The bootloader cannot be replaced in the field today: the HAL
 * allow-list refuses every write/erase intersecting the board-declared
 * bootloader region, and on STM32F4 the region is hardware write-protected
 * via WRP option bytes (foundation PR-EBT-3, eBoot #191). This header is
 * the privileged path through that guard, per
 * docs/design/bootloader-replacement-entry.md.
 *
 * The privilege boundary has three sides, and this header only carries the
 * first:
 *
 * 1. Request side (stage-1 / application). The application, or stage-1's
 *    own update flow, stages a complete candidate stage-1 image into the
 *    recovery slot through the existing streaming update path, then calls
 *    eos_bootloader_replace_request(). That verifies the staged image,
 *    writes the replacement-intent record into bootctl, and resets. The
 *    application never writes the bootloader region and never calls any
 *    replacement function -- the HAL allow-list refuses it.
 * 2. Execution side (stage-0 only). On the next boot, stage-0 -- running
 *    privileged, interrupts disabled, before any application code and
 *    before jumping to stage-1 -- runs the replacement executor
 *    (eblr_bootloader_replace_execute(), declared in the stage-0-local
 *    stage0/bootloader_replace.h, not here): re-verify the staged image,
 *    lift WRP, copy, re-apply WRP, verify the written bytes, clear the
 *    record, reset. There is no callable entry point from unprivileged
 *    code.
 * 3. The single exemption. The HAL allow-list keeps refusing the region
 *    for every caller. The executor reaches it through the privileged
 *    HAL entries (eos_hal_flash_write_bootloader() /
 *    eos_hal_flash_erase_bootloader()), gated by a one-shot RAM
 *    authorization token minted only after the staged image has fully
 *    verified.
 *
 * Scope: stage-1 is replaceable in-band. Stage-0 is not, ever -- it is
 * the immutable root of trust, and replacing it in-band has no fallback.
 *
 * Gating prerequisite (design note §3): in-band replacement ships only
 * after stage-0 verifies stage-1 by Ed25519 signature against the
 * keystore instead of by build-time hash (the Phase-2 flow in
 * docs/secure_boot_chain.md §2.2). Until that prerequisite lands, a
 * fielded device whose stage-0 pins stage-1 by build-time hash cannot
 * use in-band replacement: the hash-pinned stage-0 would refuse the new
 * stage-1 and enter recovery. This entry point is implemented and tested
 * now; field deployment waits on the prerequisite.
 */

#ifndef EOS_BOOTLOADER_REPLACE_H
#define EOS_BOOTLOADER_REPLACE_H

#include "eos_types.h"
#include "eos_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verify a staged stage-1 image the way the replacement gate requires.
 *
 * Runs the design note §3 steps in order against the image at @p image_addr:
 * structure (header parses, sizes fit the staging slot and the stage-1
 * region), target identity (load_addr is the board's stage-1 base,
 * entry_addr lies inside the stage-1 region, and the authenticated
 * EOS_TLV_TARGET value matches this board), SHA-256 integrity
 * (EOS_IMG_FLAG_HASH_SHA256 is required -- a CRC-only image is refused),
 * Ed25519 signature against the keystore active key, then the
 * authenticated EOS_TLV_MIN_SEC_VER counter against the device floor.
 *
 * The TLV target is checked before signature work is spent, as §3 orders
 * it; the area is authenticated against hdr->tlv_hash before the value is
 * trusted, and the counter is read through eos_rollback_read_image_counter()
 * which re-authenticates the area, so a forged TLV is refused at the
 * signature step at the latest.
 *
 * @param image_addr  Flash address of the staged image header.
 * @param hdr_out     Receives the parsed header on success. May be NULL.
 * @param counter_out Receives the image's EOS_TLV_MIN_SEC_VER on success.
 *                    May be NULL.
 * @return EOS_OK on success; EOS_ERR_INVALID for a malformed header, a
 *         target mismatch, a CRC-only image, or a record that fails the
 *         TLV binding; EOS_ERR_CRC for a payload hash mismatch;
 *         EOS_ERR_SIGNATURE / EOS_ERR_KEY for signature failures;
 *         EOS_ERR_ANTI_ROLLBACK for a counter below the device floor;
 *         EOS_ERR_NOT_SUPPORTED when the board declares no bootloader
 *         region, no board identity, or no monotonic counter.
 */
int eos_bootloader_replace_verify_image(uint32_t image_addr,
                                        eos_image_header_t *hdr_out,
                                        uint32_t *counter_out);

/**
 * @brief Request a stage-1 replacement (stage-1 side only).
 *
 * Verifies the staged image in @p staging_slot, writes the
 * replacement-intent record (PENDING, with expected digest / counter /
 * size) into bootctl, and resets. Stage-0 performs the replacement on
 * the next boot. This function never touches the bootloader region.
 *
 * @param staging_slot  Must be EOS_SLOT_RECOVERY: the recovery slot is
 *                      the staging area (design note §11 Q2 -- a lost
 *                      recovery image is recoverable over UART, a lost
 *                      confirmed-good application image is not).
 * @return EOS_OK on success (a reset was requested; the call does not
 *         return on a device); EOS_ERR_INVALID for a staging slot other
 *         than EOS_SLOT_RECOVERY or a slot that intersects the
 *         bootloader region; EOS_ERR_BUSY if a replacement is already
 *         in progress; EOS_ERR_NOT_SUPPORTED if the board declares no
 *         region, no board identity, or no unprotect primitive; or the
 *         verification error from eos_bootloader_replace_verify_image().
 */
int eos_bootloader_replace_request(eos_slot_t staging_slot);

/**
 * @brief Commit the running stage-1 image's security counter.
 *
 * Call after stage-1 has completed a full boot decision -- scanned the
 * slots, selected a verified application image, and is about to jump to
 * it. Reads this stage-1 image's own EOS_TLV_MIN_SEC_VER from flash,
 * stages it, and commits it, raising the device floor to the counter of
 * the stage-1 image that just proved itself (design note §4, §10 step
 * 10). Idempotent: a counter at or below the floor, or no counter at
 * all, commits nothing. A failure is not propagated -- the boot decision
 * already succeeded, and the condition is observable via
 * eos_rollback_get_device_counter() -- mirroring eos_bootctl_confirm().
 *
 * The floor is shared with the application update path: one device
 * floor, one epoch. A stage-1 release bumps its counter only on a
 * security event or key rotation, in step with the application train
 * (design note §4), so committing it here cannot strand a current
 * application image.
 */
int eos_bootloader_replace_commit_counter(void);

#ifdef __cplusplus
}
#endif
#endif /* EOS_BOOTLOADER_REPLACE_H */
