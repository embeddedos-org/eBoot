// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file jump_stage1.c
 * @brief Stage-0 jump to Stage-1 (E-Boot)
 *
 * Performs the clean handoff from stage-0 to stage-1.
 * Disables interrupts, deinitializes peripherals, and
 * branches to the stage-1 vector table.
 */

#include "eos_hal.h"
#include "eos_bootctl.h"

/* Forward declarations */
extern void ebldr_watchdog_init(void);
extern void ebldr_watchdog_feed(void);
extern bool ebldr_recovery_triggered(const eos_bootctl_t *bctl);
extern int  eos_recovery_enter(eos_bootctl_t *bctl);

/* Forward declarations from boot_log */
extern void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail);

/**
 * @brief Stage-0 main entry point.
 *
 * Called after hw_init_minimal(). Loads boot control block,
 * checks recovery triggers, and jumps to stage-1.
 */
void ebldr_stage0_main(void)
{
    eos_bootctl_t bctl;

    /* Initialize watchdog */
    ebldr_watchdog_init();
    ebldr_watchdog_feed();

    /* Load boot control block */
    int rc = eos_bootctl_load(&bctl);
    (void)rc; /* defaults applied if both copies corrupt */

    /* Record reset reason */
    bctl.last_reset_reason = eos_hal_get_reset_reason();

    /* Log boot start */
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_NONE,
                        bctl.last_reset_reason);

    /* Check for recovery triggers */
    if (ebldr_recovery_triggered(&bctl)) {
        eos_recovery_enter(&bctl);
        /* Does not return unless recovery instructs a reboot */
    }

    ebldr_watchdog_feed();

    /* Verify stage-1 integrity before jumping (secure boot chain) */
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (ops && ops->jump) {
        uint32_t stage1_addr = ops->flash_base + ops->app_vector_offset;

#ifdef EBLDR_VERIFY_STAGE1
        /* Compute SHA-256 of stage-1 and compare against build-time hash */
        extern const uint8_t stage1_expected_hash[32];
        extern const uint32_t stage1_expected_size;

        uint8_t computed[32];
        eos_sha256_ctx_t sha_ctx;
        eos_sha256_init(&sha_ctx);

        uint8_t buf[256];
        uint32_t off = 0;
        while (off < stage1_expected_size) {
            uint32_t chunk = stage1_expected_size - off;
            if (chunk > sizeof(buf)) chunk = sizeof(buf);
            eos_hal_flash_read(stage1_addr + off, buf, chunk);
            eos_sha256_update(&sha_ctx, buf, chunk);
            off += chunk;
        }
        eos_sha256_final(&sha_ctx, computed);

        /* Double-check for fault injection resistance */
        volatile int match1 = 0, match2 = 0;
        for (int i = 0; i < 32; i++) {
            if (computed[i] != stage1_expected_hash[i]) match1 = 1;
        }
        for (int i = 31; i >= 0; i--) {
            if (computed[i] != stage1_expected_hash[i]) match2 = 1;
        }

        if (match1 || match2) {
            eos_boot_log_append(EOS_LOG_BOOT_FAIL, EOS_SLOT_NONE, 0xBAD1);
            eos_recovery_enter(&bctl);
        }
        eos_boot_log_append(EOS_LOG_IMAGE_VALID, EOS_SLOT_NONE, 0);
#endif

        /* Stage-1 is located immediately after stage-0 in flash */
        eos_hal_disable_interrupts();
        eos_hal_deinit_peripherals();
        ops->jump(stage1_addr);
    }

    /* If jump fails, enter recovery */
    eos_recovery_enter(&bctl);
}
