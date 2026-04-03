// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file debug_lock.c
 * @brief Debug interface (JTAG/SWD) lock management
 *
 * Disables debug probes based on build-time policy. Essential
 * for preventing physical attacks via debug interfaces.
 */

#include "eos_debug_lock.h"
#include "eos_hal.h"

#ifndef EBLDR_DEBUG_LOCK_POLICY
#define EBLDR_DEBUG_LOCK_POLICY EOS_DEBUG_LOCK_PRODUCTION
#endif

int eos_debug_lock_apply(void)
{
    eos_debug_lock_policy_t policy = (eos_debug_lock_policy_t)EBLDR_DEBUG_LOCK_POLICY;

    if (policy == EOS_DEBUG_LOCK_NEVER) {
        return EOS_OK;
    }

    if (policy == EOS_DEBUG_LOCK_PRODUCTION) {
        /* Only lock if not in debug build */
#ifdef EBLDR_DEBUG_BUILD
        return EOS_OK;
#endif
    }

    /* Attempt to lock via HAL */
    return eos_hal_debug_lock();
}

int eos_debug_lock_verify(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;

    int rc = eos_hal_debug_status(status);
    if (rc == EOS_ERR_NOT_SUPPORTED) {
        /* If HAL doesn't support status query, check via CoreSight */
#if defined(__ARM_ARCH) && (__ARM_ARCH <= 7) && !defined(__aarch64__)
        volatile uint32_t *dhcsr = (volatile uint32_t *)0xE000EDF0;
        if (*dhcsr & (1U << 0)) { /* C_DEBUGEN */
            *status = EOS_DEBUG_STATUS_UNLOCKED;
        } else {
            *status = EOS_DEBUG_STATUS_LOCKED;
        }
        return EOS_OK;
#else
        *status = EOS_DEBUG_STATUS_UNLOCKED;
        return EOS_ERR_NOT_SUPPORTED;
#endif
    }

    return rc;
}

bool eos_debug_is_attached(void)
{
#if defined(__ARM_ARCH) && (__ARM_ARCH <= 7) && !defined(__aarch64__)
    volatile uint32_t *dhcsr = (volatile uint32_t *)0xE000EDF0;
    return (*dhcsr & (1U << 0)) != 0;
#elif defined(__ARM_ARCH_7R__)
    uint32_t dscr;
    __asm volatile ("mrc p14, 0, %0, c0, c1, 0" : "=r"(dscr));
    return (dscr & (1U << 14)) != 0; /* HALTED bit */
#else
    return false;
#endif
}
