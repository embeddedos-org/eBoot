// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_debug_lock.h
 * @brief Debug interface (JTAG/SWD) lock management
 *
 * Provides APIs to disable debug probes in production builds.
 * Controlled by build-time EBLDR_DEBUG_LOCK option:
 *   ALWAYS     — lock at every boot
 *   PRODUCTION — lock only if debug fuse is blown
 *   NEVER      — never lock (development only)
 */

#ifndef EOS_DEBUG_LOCK_H
#define EOS_DEBUG_LOCK_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_DEBUG_STATUS_UNLOCKED  0x00
#define EOS_DEBUG_STATUS_LOCKED    0x01
#define EOS_DEBUG_STATUS_PERMANENT 0x02

typedef enum {
    EOS_DEBUG_LOCK_ALWAYS     = 0,
    EOS_DEBUG_LOCK_PRODUCTION = 1,
    EOS_DEBUG_LOCK_NEVER      = 2,
} eos_debug_lock_policy_t;

/**
 * @brief Apply debug lock policy based on build configuration.
 * @return EOS_OK on success, EOS_ERR_NOT_SUPPORTED if HAL lacks debug_lock.
 */
int eos_debug_lock_apply(void);

/**
 * @brief Verify that debug interfaces are in expected state.
 * @param status  Receives current debug lock status.
 * @return EOS_OK on success.
 */
int eos_debug_lock_verify(uint32_t *status);

/**
 * @brief Check if a debugger is currently attached.
 * @return true if debugger detected, false otherwise.
 */
bool eos_debug_is_attached(void);

#ifdef __cplusplus
}
#endif
#endif /* EOS_DEBUG_LOCK_H */
