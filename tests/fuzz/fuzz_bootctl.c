// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_bootctl.c
 * @brief libFuzzer harness for boot control block (BCB) parsing
 *
 * Feeds arbitrary data into the boot control block parser, exercising
 * magic-number validation, slot metadata decoding, retry counters,
 * and CRC integrity checks.
 */

#include <stdint.h>
#include <stddef.h>

/* Forward-declare boot control block parser */
extern int eos_bootctl_parse(const void *bcb_data, size_t bcb_len);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 8) {
        return 0;
    }

    eos_bootctl_parse(data, size);

    return 0;
}
