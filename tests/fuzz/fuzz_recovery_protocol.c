// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_recovery_protocol.c
 * @brief libFuzzer harness for recovery protocol packet parsing
 *
 * Exercises the recovery-mode packet parser with arbitrary byte streams,
 * targeting framing, command dispatch, and length-field validation.
 */

#include <stdint.h>
#include <stddef.h>

/* Forward-declare recovery protocol handler */
extern int eos_recovery_parse_packet(const uint8_t *pkt, size_t pkt_len);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) {
        return 0;
    }

    eos_recovery_parse_packet(data, size);

    return 0;
}
