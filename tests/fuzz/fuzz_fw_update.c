// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_fw_update.c
 * @brief libFuzzer harness for the firmware update ingest path.
 *
 * This harness used to declare three functions of its own:
 *
 *     eos_fw_update_init(), eos_fw_update_process_chunk(), eos_fw_update_finalize(void)
 *
 * The first two have never existed. The third does exist but takes
 * (ctx, mode), not (void) -- so had the other two ever resolved, this would
 * have called it through a wrong prototype. The real ingest path is
 * begin -> write -> finalize/abort over a context the caller owns.
 *
 * Chunk widths come from the input rather than being fixed, so the fuzzer can
 * split the same payload across write() calls differently -- which is where
 * state carried between chunks goes wrong.
 */

#include "eos_fw_update.h"
#include "fuzz_sim_flash.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    eos_fw_update_ctx_t ctx;
    size_t offset = 0;
    uint8_t selector;

    if (size < 2) return 0;

    selector = data[0];
    data += 1; size -= 1;

    fuzz_flash_load(0x4000, NULL, 0);

    memset(&ctx, 0, sizeof ctx);
    if (eos_fw_update_begin(&ctx, (selector & 1) ? EOS_SLOT_B : EOS_SLOT_A) != EOS_OK)
        return 0;

    while (offset < size) {
        /* 1..64 bytes, chosen by the data itself. */
        size_t chunk = (size_t)(data[offset] & 0x3F) + 1;
        if (chunk > size - offset) chunk = size - offset;
        if (eos_fw_update_write(&ctx, data + offset, chunk) != EOS_OK) break;
        offset += chunk;
    }

    if (selector & 2)
        (void)eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST);
    else
        eos_fw_update_abort(&ctx);

    return 0;
}
