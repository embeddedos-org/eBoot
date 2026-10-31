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
#include "eos_crypto_boot.h"

/* Forward declarations */
extern void ebldr_watchdog_init(void);
extern void ebldr_watchdog_feed(void);
extern bool ebldr_recovery_triggered(const eos_bootctl_t *bctl);
extern int  eos_recovery_enter(eos_bootctl_t *bctl);

/* Forward declarations from boot_log */
extern void eos_boot_log_init(uint32_t head);
extern void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail);
extern uint32_t eos_boot_log_get_head(void);

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

    /* Open the boot log at the head the control block persisted. Until
     * this runs eos_boot_log_append() is a no-op, so every stage-0 entry
     * below was silently dropped. */
    eos_boot_log_init(bctl.log_head);

    /* Record reset reason */
    bctl.last_reset_reason = eos_hal_get_reset_reason();

    /* Log boot start */
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_NONE,
                        bctl.last_reset_reason);

    /* Check for recovery triggers */
    if (ebldr_recovery_triggered(&bctl)) {
        eos_recovery_enter(&bctl);
        /* Recovery does not return today; if it ever does, a requested
         * recovery session must not fall through into a normal boot. */
        return;
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

        /* The build embeds these from the stage-1 binary it linked. A size
         * of zero means it linked nothing (tools/embed_stage1_hash.py now
         * refuses that, but a bootloader does not trust its own build to
         * have been correct): the loop below would run zero iterations,
         * the digest of nothing would match the digest of nothing, and
         * IMAGE_VALID would be recorded for an image that does not exist. */
        if (stage1_expected_size == 0) {
            eos_boot_log_append(EOS_LOG_BOOT_FAIL, EOS_SLOT_NONE, EBLDR_FAIL_STAGE1_NO_IMAGE);
            eos_recovery_enter(&bctl);
            return;
        }

        uint8_t computed[32];
        eos_sha256_ctx_t sha_ctx;
        eos_sha256_init(&sha_ctx);

        uint8_t buf[256];
        uint32_t off = 0;
        while (off < stage1_expected_size) {
            uint32_t chunk = stage1_expected_size - off;
            if (chunk > sizeof(buf)) chunk = sizeof(buf);
            /* A read that fails leaves buf holding the previous chunk, or
             * whatever the stack held, and the result was hashed as if it
             * were stage-1. That happened to mismatch, so the failure mode
             * was recovery with the wrong reason; a read failure is its
             * own reason and is refused as such, like eos_crypto_verify_image()
             * refuses it in core. */
            if (eos_hal_flash_read(stage1_addr + off, buf, chunk) != EOS_OK) {
                eos_boot_log_append(EOS_LOG_BOOT_FAIL, EOS_SLOT_NONE, EBLDR_FAIL_STAGE1_READ);
                eos_recovery_enter(&bctl);
                return;
            }
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

        /* A mismatch enters recovery and stops here. The only thing that
         * used to keep it from the jump below was eos_recovery_enter()
         * never returning -- an invariant its own `int` return type does
         * not promise -- and IMAGE_VALID was appended on the way past. */
        if (match1 || match2) {
            eos_boot_log_append(EOS_LOG_BOOT_FAIL, EOS_SLOT_NONE, EBLDR_FAIL_STAGE1_HASH);
            eos_recovery_enter(&bctl);
            return;
        }
        eos_boot_log_append(EOS_LOG_IMAGE_VALID, EOS_SLOT_NONE, 0);
#endif

        /* Stage-1 is a separate image that re-opens the log at the head it
         * loads from the control block. Persist the head past stage-0's
         * entries, or stage-1 appends over them. A failed save costs the
         * log, not the boot, so it does not stop the jump. */
        bctl.log_head = eos_boot_log_get_head();
        (void)eos_bootctl_save(&bctl);

        /* Stage-1 is located immediately after stage-0 in flash */
        eos_hal_disable_interrupts();
        eos_hal_deinit_peripherals();
        ops->jump(stage1_addr);
    }

    /* If jump fails, enter recovery */
    eos_recovery_enter(&bctl);
}
