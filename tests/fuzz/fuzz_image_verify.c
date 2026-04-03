// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_image_verify.c
 * @brief libFuzzer harness for image header parsing and verification
 *
 * Feeds fuzzer-generated data into the image header parser via a simulated
 * flash-backed buffer, exercising bounds checks, magic-number validation,
 * and field-range assertions.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Forward-declare the image header parser */
extern int eos_image_parse_header(const void *flash_base, size_t flash_len);

/**
 * Simulated flash read-back: the fuzzer data is treated as raw flash content
 * starting at offset 0.  This lets the parser exercise its flash-pointer
 * arithmetic on arbitrary byte sequences.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) {
        return 0;
    }

    eos_image_parse_header(data, size);

    return 0;
}
