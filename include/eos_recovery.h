// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_recovery.h
 * @brief UART recovery mode entry and write-range checks
 */

#ifndef EOS_RECOVERY_H
#define EOS_RECOVERY_H

#include "eos_bootctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return EOS_OK if a recovery write of @p len bytes at @p offset
 *        stays inside the slot at @p base.
 *
 * Rejects a zero base or slot size, a zero length, a write that runs
 * past the slot, and wrap of @c base + offset.
 */
int eos_recovery_write_in_range(uint32_t base, uint32_t slot_size,
                                uint32_t offset, uint16_t len);

/**
 * @brief Enter the UART recovery command loop. Does not return on success.
 */
int eos_recovery_enter(eos_bootctl_t *bctl);

#ifdef __cplusplus
}
#endif
#endif /* EOS_RECOVERY_H */
