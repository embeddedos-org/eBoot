// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_image_verify.c
 * @brief libFuzzer harness for image header parsing and verification.
 *
 * This harness used to declare the parser itself:
 *
 *     extern int eos_image_parse_header(const void *flash_base, size_t flash_len);
 *
 * The real one is `int eos_image_parse_header(uint32_t addr,
 * eos_image_header_t *out)`. The names matched so it linked, and every call
 * passed a pointer where an address was expected and a size where an output
 * struct was expected. It fuzzed nothing and was undefined behaviour doing it.
 * Including the header instead means the compiler checks this from now on.
 */

#include "eos_image.h"
#include "fuzz_sim_flash.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const uint32_t addr = 0x4000;   /* slot A */
    eos_image_header_t hdr;

    if (size < sizeof(eos_image_header_t)) return 0;

    fuzz_flash_load(addr, data, size);

    memset(&hdr, 0, sizeof hdr);
    if (eos_image_parse_header(addr, &hdr) == EOS_OK) {
        /* Only a header the parser accepted reaches verification, which is
         * the same order the boot path uses. */
        (void)eos_image_verify_integrity(&hdr, addr);
    }
    return 0;
}
