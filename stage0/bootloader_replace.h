// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file bootloader_replace.h
 * @brief Stage-0 replacement executor (ADR-022) -- stage-0 internal
 *
 * This header is stage-0-local on purpose: the executor is a stage-0
 * internal function, not a public-header API (design note §9.2). Stage-1
 * and application code never include this directory, so they cannot call
 * the executor, mint the token, or reach the privileged HAL entries
 * through these declarations. The request side is the public API in
 * include/eos_bootloader_replace.h.
 *
 * The one-shot RAM authorization token is declared here too, for the same
 * reason: only the executor mints it, and only after the staged image has
 * fully verified. Its definition lives in hal/hal_core.c, next to the
 * privileged entry points it gates.
 */

#ifndef EBLDR_BOOTLOADER_REPLACE_H
#define EBLDR_BOOTLOADER_REPLACE_H

#include "eos_types.h"
#include "eos_bootctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Executor outcomes (eblr_bootloader_replace_execute() return value). */
#define EBLR_REPLACE_PROCEED        0  /* boot stage-1 normally */
#define EBLR_REPLACE_RESET_PENDING  1  /* transaction advanced; a reset was
                                          requested; the caller must NOT jump */
/* Negative values are EOS_ERR_* codes. The caller distinguishes two:
 *   EOS_ERR_NO_IMAGE -- the staged image cannot be verified and stage-1
 *                       may be partial (COPYING): enter recovery.
 *   anything else    -- fatal (WRP lift, copy, or WRP re-apply failed):
 *                       halt; do not boot, do not enter recovery. */

/**
 * @brief Mint the one-shot RAM authorization token for the privileged
 * HAL write/erase entries. Stage-0 only; the executor is the only
 * caller, and it mints only after the staged image has fully verified.
 * The token is cleared by eos_hal_bootloader_token_clear() when the
 * authorized copy ends, and never survives a reset.
 */
void eos_hal_bootloader_token_mint(void);

/**
 * @brief Clear the one-shot RAM authorization token. The executor calls
 * this when the authorized copy ends, success or failure.
 */
void eos_hal_bootloader_token_clear(void);

/**
 * @brief Execute a pending stage-1 replacement (stage-0 only).
 *
 * Called from ebldr_stage0_main() after bootctl is loaded and before
 * jumping to stage-1. Runs the design note §10 transaction:
 *
 *   PENDING: re-verify the staged image (§3) and cross-check it against
 *   the intent record; if the installed stage-1 already is the staged
 *   image, re-apply WRP, clear the record, and boot. Otherwise lift WRP,
 *   record COPYING, and -- on boards whose protection changes take
 *   effect only at reset -- reset so the copy runs on the next boot.
 *
 *   COPYING: the staged image re-verifies, then the executor mints the
 *   token, erases the stage-1 sectors, copies the staged payload,
 *   verifies the written bytes by re-hash, re-applies WRP, clears the
 *   record, and resets.
 *
 * Refusals clear the intent record and boot the old stage-1; a COPYING
 * boot whose staged image no longer verifies goes to recovery (the
 * record stays COPYING); WRP lift, copy, and WRP re-apply failures halt.
 *
 * @param bctl  Boot control block loaded by the caller.
 * @return EBLR_REPLACE_PROCEED, EBLR_REPLACE_RESET_PENDING, or an
 *         EOS_ERR_* code as documented above.
 */
int eblr_bootloader_replace_execute(const eos_bootctl_t *bctl);

#ifdef __cplusplus
}
#endif
#endif /* EBLDR_BOOTLOADER_REPLACE_H */
