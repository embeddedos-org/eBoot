// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file bootloader_protect.c
 * @brief Bootloader self-protection policy (foundation PR-EBT-3, S3)
 *
 * Programs the board's hardware write protection for the bootloader's
 * own flash region, per the build-time policy. Mirrors core/debug_lock.c:
 * the policy decides whether to call the HAL at all; the HAL decides
 * whether the board can do it. A board that declares a bootloader
 * region but provides no protect op reports EOS_ERR_NOT_SUPPORTED --
 * never a silent success -- so a production build that ignores the
 * return value is the only way this fails open, and the header says
 * so in so many words.
 */

#include "eos_bootloader_protect.h"
#include "eos_hal.h"

#ifndef EBLDR_BOOTLOADER_PROTECT_POLICY
#define EBLDR_BOOTLOADER_PROTECT_POLICY EOS_BOOTLOADER_PROTECT_PRODUCTION
#endif

int eos_bootloader_protect_apply(void)
{
    eos_bootloader_protect_policy_t policy =
        (eos_bootloader_protect_policy_t)EBLDR_BOOTLOADER_PROTECT_POLICY;

    /* cppcheck-suppress knownConditionTrueFalse  // policy is EBLDR_BOOTLOADER_PROTECT_POLICY, a build-time choice; the branch is dead only for the default this build selected */
    if (policy == EOS_BOOTLOADER_PROTECT_NEVER) {
        return EOS_OK;
    }

    /* cppcheck-suppress knownConditionTrueFalse  // same: a different -DEBLDR_BOOTLOADER_PROTECT_POLICY takes the other branch */
    if (policy == EOS_BOOTLOADER_PROTECT_PRODUCTION) {
        /* Only protect if not in debug build */
#ifdef EBLDR_DEBUG_BUILD
        return EOS_OK;
#endif
    }

    /* Attempt to protect via HAL */
    return eos_hal_flash_protect_bootloader();
}

int eos_bootloader_protect_verify(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;

    return eos_hal_flash_protect_status(status);
}
