// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file main.c
 * @brief Stage-1 (E-Boot) main entry point
 *
 * The stage-1 boot manager: loads boot state, scans images,
 * applies boot policy, and jumps to the selected firmware.
 */

#include "eos_bootctl.h"
#include "eos_image.h"
#include "eos_hal.h"
#include "eos_mpu_boot.h"
#include "eos_bootloader_replace.h"

/* Forward declarations */
extern void eos_boot_log_init(uint32_t head);
extern int  eboot_scan_images(eos_bootctl_t *bctl);
extern int  eboot_select_slot(eos_bootctl_t *bctl, eos_slot_t *out);
extern bool eboot_should_recover(const eos_bootctl_t *bctl);
extern int  eboot_jump_to_app(eos_bootctl_t *bctl, eos_slot_t slot);
extern int  eos_recovery_enter(eos_bootctl_t *bctl);
extern void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail);

/**
 * @brief E-Boot main — stage-1 boot manager.
 *
 * Boot decision order:
 * 1. Load boot metadata
 * 2. Initialize boot log
 * 3. Check recovery triggers
 * 4. Scan firmware slots
 * 5. Select boot target
 * 6. Jump to application
 * 7. Enter recovery if all else fails
 */
int eboot_main(void)
{
    eos_bootctl_t bctl;

    /* Load boot control block */
    eos_bootctl_load(&bctl);

    /* Initialize boot log with saved head position */
    eos_boot_log_init(bctl.log_head);

    /* Record reset reason */
    bctl.last_reset_reason = eos_hal_get_reset_reason();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_NONE,
                        bctl.last_reset_reason);

    eos_hal_watchdog_feed();

    /* Check for explicit recovery / factory reset requests */
    if (eboot_should_recover(&bctl)) {
        /* Does not return on success. On failure it returns the entry
         * save error (#215): recovery was requested, so falling through
         * to a normal boot here would boot an image the operator did
         * not ask for. Return instead; the reset entry halts. */
        return eos_recovery_enter(&bctl);
    }

    /* Scan all firmware slots */
    eboot_scan_images(&bctl);

    eos_hal_watchdog_feed();

    /* Select the boot target */
    eos_slot_t target = EOS_SLOT_NONE;
    int rc = eboot_select_slot(&bctl, &target);

    if (rc == EOS_OK && target != EOS_SLOT_NONE) {
        eos_boot_log_append(EOS_LOG_SLOT_SELECTED, target, 0);

        /* The boot decision is complete: this stage-1 image has
         * proven itself. Commit its security counter (ADR-022 §4),
         * so the device floor rises to the epoch of the stage-1
         * that just booted. Idempotent and ignored on failure:
         * the slot is selected and that must stick, mirroring
         * eos_bootctl_confirm(). */
        (void)eos_bootloader_replace_commit_counter();

        /* Jump to the selected application */
        rc = eboot_jump_to_app(&bctl, target);

        /* If jump returns, something went wrong */
        eos_boot_log_append(EOS_LOG_BOOT_FAIL, target, rc);
    }

    /* No bootable image — enter recovery */
    eos_boot_log_append(EOS_LOG_RECOVERY_ENTER, EOS_SLOT_NONE, 0);
    eos_recovery_enter(&bctl);

    /* Should never reach here */
    while (1) {
        eos_hal_watchdog_feed();
    }

    return EOS_ERR_GENERIC;
}
