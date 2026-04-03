// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_fw_update.c
 * @brief libFuzzer harness for firmware update stream processing
 *
 * Simulates a chunked firmware-update data stream, feeding arbitrary data
 * into the update parser to exercise header validation, chunk sequencing,
 * checksum verification, and boundary conditions.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Forward-declare firmware update APIs */
extern int eos_fw_update_init(void);
extern int eos_fw_update_process_chunk(const uint8_t *chunk, size_t chunk_len);
extern int eos_fw_update_finalize(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) {
        return 0;
    }

    eos_fw_update_init();

    /* Feed data in variable-sized chunks derived from the fuzzer input */
    size_t offset = 0;
    while (offset < size) {
        size_t chunk_sz = (data[offset] % 64) + 1;
        if (offset + chunk_sz > size) {
            chunk_sz = size - offset;
        }
        eos_fw_update_process_chunk(data + offset, chunk_sz);
        offset += chunk_sz;
    }

    eos_fw_update_finalize();

    return 0;
}
