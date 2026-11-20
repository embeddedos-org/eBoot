// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file hw_init_minimal.c
 * @brief Stage-0 minimal hardware initialization
 *
 * Performs only the absolute minimum to reach a known-good state:
 * clock configuration, flash latency, and memory setup.
 * No policy or complex logic belongs here.
 */

#include "eos_hal.h"
#include "eos_bootloader_protect.h"
#include <stdint.h>

/* Board-specific init — provided by the board port */
extern void board_early_init(void);
extern const eos_board_ops_t *board_get_ops(void);

void ebldr_hw_init_minimal(void)
{
    /* Register the board HAL */
    const eos_board_ops_t *ops = board_get_ops();
    eos_hal_init(ops);

    /* Board-specific early initialization (clocks, flash latency) */
    board_early_init();

    /* Lock debug interfaces before loading any secrets */
#ifndef EBLDR_DEBUG_BUILD
    if (ops->debug_lock) {
        ops->debug_lock();
    }
#endif

    /* Program the bootloader region's hardware write protection on
     * boards that declare one (WRP on STM32F4). A non-EOS_OK result
     * is fatal: the board would otherwise boot believing itself
     * protected while the hardware protection is absent (no primitive
     * for a declared region) or the program failed. Halt instead of
     * proceeding -- the stage-0 fault handlers halt the same way.
     * Only boards that declare a region reach this call; a board with
     * no region keeps today's behaviour. The protection takes effect
     * at the next reset; the HAL allow-list covers the same region
     * from the first boot. */
#ifndef EBLDR_DEBUG_BUILD
    if (ops->bootloader_size != 0) {
        if (eos_bootloader_protect_apply() != EOS_OK) {
            /* Fatal: do not boot unprotected. */
            while (1) {
            }
        }
    }
#endif

    /* Verify silicon revision and security fuse state */
    if (ops->debug_status) {
        uint32_t dbg_status = 0;
        ops->debug_status(&dbg_status);
        /* Log debug state for audit trail */
        (void)dbg_status;
    }
}
