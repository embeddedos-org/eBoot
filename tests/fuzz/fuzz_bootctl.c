// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_bootctl.c
 * @brief libFuzzer harness for boot control block loading and validation.
 *
 * This harness used to declare and call `eos_bootctl_parse()`. No such
 * function exists in this repository -- the name appears nowhere outside this
 * file -- so the target never linked, and a fuzz target that does not link
 * proves nothing about the code it names.
 *
 * The real untrusted-input path is eos_bootctl_load(), which reads the block
 * out of flash and is where a corrupt or hostile BCB arrives. The fuzz input
 * is written to the primary and backup BCB addresses and loaded from there.
 */

#include "eos_bootctl.h"
#include "fuzz_sim_flash.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    eos_bootctl_t bctl;

    if (size < 8) return 0;

    /* bootctl_addr is 0 and bootctl_backup_addr is 0x1000 in fuzz_sim_ops, so
     * one load can see fuzzer bytes in both the primary and the backup. */
    fuzz_flash_load(0, data, size);
    fuzz_flash_write(0x1000, data, size < 0x1000 ? size : 0x1000);

    memset(&bctl, 0, sizeof bctl);
    if (eos_bootctl_load(&bctl) == EOS_OK) {
        /* Anything load() accepted must also pass validate(): the two
         * disagreeing is how a corrupt block reaches the boot decision. */
        if (!eos_bootctl_validate(&bctl)) __builtin_trap();

        (void)eos_bootctl_increment_attempts(&bctl);
        (void)eos_bootctl_clear_pending(&bctl);
    }
    return 0;
}
