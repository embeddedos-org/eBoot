// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_fdt.c
 * @brief libFuzzer harness for the flattened device tree parser
 *
 * The DTB is off-device input: it comes out of flash or from a prior boot
 * stage, and every offset and length in its header is attacker-controlled.
 * .ai/security.md names device tree among the parsers that "get fuzz
 * coverage, not just unit tests"; core/fdt_loader.c had neither until
 * recently, and the ten hand-written blobs in tests/unit/test_fdt_loader.c
 * cover the shapes that were reasoned about rather than the ones nobody
 * thought of.
 *
 * The size-carrying entry points are the ones driven here. Passing `size`
 * is what makes the harness meaningful: the unsized forms take the blob's
 * own totalsize as the bound, so a fuzzer that inflates that field would be
 * telling the parser it may read past the buffer libFuzzer allocated, and
 * every report would be the harness's fault rather than the parser's.
 */

#include "eos_fdt_loader.h"

#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > UINT32_MAX) {
        return 0;
    }

    uint32_t len = (uint32_t)size;

    if (eos_fdt_validate(data, len) != 0) {
        return 0;
    }

    /* Only reached for a blob whose header survived validation, which is
     * where the interesting walking bugs live. Both a path that exists in
     * most trees and one that does not, so the FDT_END and not-found exits
     * are exercised as well as the match. */
    unsigned char buf[256];
    uint32_t buf_len = sizeof buf;
    (void)eos_fdt_get_prop(data, len, "/chosen", "bootargs",
                                 buf, &buf_len);

    buf_len = sizeof buf;
    (void)eos_fdt_get_prop(data, len, "/", "compatible",
                                 buf, &buf_len);

    /* A one-byte buffer drives the -7 truncation path, which is the branch
     * that reports a length back to the caller. */
    unsigned char tiny[1];
    buf_len = sizeof tiny;
    (void)eos_fdt_get_prop(data, len, "/", "bootargs", tiny, &buf_len);

    return 0;
}
