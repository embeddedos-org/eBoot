// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_bootloader_protect.h
 * @brief Bootloader self-protection: keep the bootloader's own flash
 * region out of every write/erase path, and program the hardware write
 * protection for it where the board has one.
 *
 * Two layers, and they do not depend on each other:
 *
 * 1. The HAL allow-list (hal/hal_core.c) refuses any flash write or
 *    erase intersecting the board-declared bootloader region
 *    (eos_board_ops_t.bootloader_addr/size). It needs no call: every
 *    write through eos_hal_flash_write()/eos_hal_flash_erase() is
 *    checked. A board that declares no region gets no enforcement.
 *
 * 2. The hardware protection (this file) programs the board's
 *    write-protection primitive for that region -- STM32F4 WRP option
 *    bytes today -- and verifies it by read-back. The integrator calls
 *    eos_bootloader_protect_apply(), e.g. from stage-0 init after
 *    board_early_init(); a production build must treat a non-EOS_OK
 *    result as fatal, because a board that declares a region but has
 *    no primitive reports EOS_ERR_NOT_SUPPORTED rather than success.
 *    The allow-list still refuses bootloader-region writes in that
 *    case, but the hardware protection is absent, and silently
 *    proceeding as if protected would be the fail-open shape this
 *    program exists to remove.
 *
 * Controlled by build-time EBLDR_BOOTLOADER_PROTECT_POLICY option:
 *   ALWAYS     — protect at every boot
 *   PRODUCTION — protect only if not a debug build
 *   NEVER      — never protect (development only)
 *
 * The protection takes effect at the next reset on parts whose option
 * bytes are reloaded on reset (STM32F4: RM0090 §3.9). There is no
 * bootloader self-update path in the tree today, so the allow-list
 * refuses the region unconditionally: a future self-update must go
 * through an explicit privileged entry point, not around this guard.
 */

#ifndef EOS_BOOTLOADER_PROTECT_H
#define EOS_BOOTLOADER_PROTECT_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_BOOTLOADER_PROTECT_UNPROTECTED 0x00
#define EOS_BOOTLOADER_PROTECT_PROTECTED   0x01

typedef enum {
    EOS_BOOTLOADER_PROTECT_ALWAYS     = 0,
    EOS_BOOTLOADER_PROTECT_PRODUCTION = 1,
    EOS_BOOTLOADER_PROTECT_NEVER      = 2,
} eos_bootloader_protect_policy_t;

/**
 * @brief Apply bootloader self-protection per the build policy.
 * @return EOS_OK on success, EOS_ERR_NOT_SUPPORTED if the HAL lacks
 *         flash_protect_bootloader (fail-closed signal: the caller
 *         must not proceed as if protected).
 */
int eos_bootloader_protect_apply(void);

/**
 * @brief Verify that the bootloader region is hardware-protected.
 * @param status  Receives EOS_BOOTLOADER_PROTECT_PROTECTED or
 *                EOS_BOOTLOADER_PROTECT_UNPROTECTED.
 * @return EOS_OK on success, EOS_ERR_NOT_SUPPORTED if the HAL lacks
 *         flash_protect_status.
 */
int eos_bootloader_protect_verify(uint32_t *status);

#ifdef __cplusplus
}
#endif
#endif /* EOS_BOOTLOADER_PROTECT_H */
