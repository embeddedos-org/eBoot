// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_rollback.h
 * @brief Anti-rollback security counter (threat model T-202).
 *
 * A signature proves an image is authentic, not that it is current. Blocking a
 * correctly signed but superseded image needs state that survives reflashing,
 * which the device monotonic counter provides.
 *
 * The counter compared here is a dedicated security counter carried in the
 * image's EOS_TLV_MIN_SEC_VER TLV, inside the signed area. It is not the
 * firmware version: the device-side counter is backed by OTP, so increments
 * consume storage that cannot be reclaimed and must be reserved for releases
 * that close a vulnerability.
 *
 * Verification and commit are separate. Raising the floor when an image is
 * accepted would make every older image unbootable while the new one is still
 * unproven, leaving no fallback if it fails. eos_rollback_verify() runs on
 * every boot; eos_rollback_commit() runs only once the booted image has been
 * confirmed good.
 */

#ifndef EOS_ROLLBACK_H
#define EOS_ROLLBACK_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Largest single advance eos_rollback_commit() will apply. */
#define EOS_ROLLBACK_MAX_STEP  16u

/**
 * @brief Read the security counter declared by an image.
 * @param image_addr   Flash address of the image header.
 * @param counter_out  Receives the declared counter (0 if no TLV present).
 * @return EOS_OK on success, negative on a malformed header or bad flash.
 */
int eos_rollback_read_image_counter(uint32_t image_addr, uint32_t *counter_out);

/**
 * @brief Read the device's current anti-rollback floor.
 * @return EOS_OK, or EOS_ERR_NOT_SUPPORTED if the board has no counter.
 */
int eos_rollback_get_device_counter(uint32_t *counter_out);

/**
 * @brief Reject an image whose security counter is below the device floor.
 *
 * Boards without a monotonic counter return EOS_OK, since failing closed would
 * make every such board unbootable. Detect that case at provisioning time via
 * eos_rollback_get_device_counter().
 *
 * @return EOS_OK if acceptable, EOS_ERR_ANTI_ROLLBACK if too old.
 */
int eos_rollback_verify(uint32_t image_counter);

/**
 * @brief Record the counter of the image being booted, for a later commit.
 */
void eos_rollback_stage(uint32_t image_counter);

/**
 * @brief Discard any staged counter without committing it.
 */
void eos_rollback_clear_staged(void);

/**
 * @brief Raise the device floor to the staged counter.
 *
 * Idempotent, and never lowers the counter.
 *
 * @return EOS_OK on success or when there is nothing to do;
 *         EOS_ERR_NOT_SUPPORTED if the board has no counter;
 *         EOS_ERR_INVALID if the advance exceeds EOS_ROLLBACK_MAX_STEP;
 *         EOS_ERR_GENERIC if the counter did not read back as expected.
 */
int eos_rollback_commit(void);

#ifdef __cplusplus
}
#endif
#endif /* EOS_ROLLBACK_H */
