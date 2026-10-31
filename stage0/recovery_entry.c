// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file recovery_entry.c
 * @brief Stage-0 recovery trigger detection
 */

#include "eos_hal.h"
#include "eos_bootctl.h"

bool ebldr_recovery_triggered(const eos_bootctl_t *bctl)
{
    /* GPIO pin held low during reset */
    if (eos_hal_recovery_pin_asserted())
        return true;

    /* Explicit recovery flag set by firmware or bootloader */
    if (bctl->flags & EOS_FLAG_FORCE_RECOVERY)
        return true;

    /* Factory reset requested */
    if (bctl->flags & EOS_FLAG_FACTORY_RESET)
        return true;

    /* Exhausted boot attempts are not a recovery trigger. An image that
     * hangs is reset by the watchdog until boot_attempts reaches
     * max_attempts, and that is the condition on which stage-1's
     * eos_boot_policy_select() rolls back to the other slot. Diverting it
     * to recovery here meant the rollback never ran. Stage-1 still enters
     * recovery itself when there is no valid slot to roll back to. */

    return false;
}
