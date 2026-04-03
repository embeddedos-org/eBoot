// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_crypto.c
 * @brief libFuzzer harness for SHA-256 and Ed25519 cryptographic routines
 *
 * Exercises the SHA-256 hash function with arbitrary-length inputs and
 * the Ed25519 verification function with fuzz-generated signatures,
 * keys, and messages.
 */

#include <stdint.h>
#include <stddef.h>

/* Forward-declare crypto APIs */
extern int eos_crypto_hash(const uint8_t *data, size_t len, uint8_t digest[32]);
extern int eos_ed25519_verify(const uint8_t signature[64],
                              const uint8_t public_key[32],
                              const uint8_t *message,
                              size_t msg_len);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t digest[32];

    /* Fuzz SHA-256: any length input must not crash */
    if (size > 0) {
        eos_crypto_hash(data, size, digest);
    }

    /* Fuzz Ed25519: need at least 64 (sig) + 32 (key) + 1 (msg) = 97 bytes */
    if (size >= 97) {
        const uint8_t *sig = data;
        const uint8_t *pub = data + 64;
        const uint8_t *msg = data + 96;
        size_t msg_len = size - 96;

        eos_ed25519_verify(sig, pub, msg, msg_len);
    }

    return 0;
}
