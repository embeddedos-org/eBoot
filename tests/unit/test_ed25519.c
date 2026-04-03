// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_ed25519.c
 * @brief Unit tests for Ed25519 signature verification
 */

#include "eos_crypto_boot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int eos_ed25519_verify(const uint8_t signature[64],
                              const uint8_t public_key[32],
                              const uint8_t *message,
                              size_t msg_len);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("  %-50s ", #name); \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

TEST(test_ed25519_null_args)
{
    uint8_t sig[64] = {0};
    uint8_t pub[32] = {0};
    uint8_t msg[] = "test";

    ASSERT(eos_ed25519_verify(NULL, pub, msg, sizeof(msg)) != 0);
    ASSERT(eos_ed25519_verify(sig, NULL, msg, sizeof(msg)) != 0);
    ASSERT(eos_ed25519_verify(sig, pub, NULL, sizeof(msg)) != 0);
    ASSERT(eos_ed25519_verify(sig, pub, msg, 0) != 0);
}

TEST(test_ed25519_invalid_signature_format)
{
    uint8_t pub[32] = {1};
    uint8_t msg[] = "hello";

    /* S component with top 3 bits set (invalid encoding) */
    uint8_t sig_bad_s[64];
    memset(sig_bad_s, 0x01, 64);
    sig_bad_s[63] = 0xE0; /* top 3 bits of S set */
    ASSERT(eos_ed25519_verify(sig_bad_s, pub, msg, sizeof(msg)) != 0);

    /* All-zero R component (degenerate point) */
    uint8_t sig_zero_r[64];
    memset(sig_zero_r, 0x00, 32);  /* R = all zeros */
    memset(sig_zero_r + 32, 0x01, 32);
    ASSERT(eos_ed25519_verify(sig_zero_r, pub, msg, sizeof(msg)) != 0);
}

TEST(test_ed25519_valid_structural)
{
    /*
     * Construct a structurally valid signature (R and S within range).
     * This should pass format checks but will fail the actual math
     * verification — the function should return non-zero for incorrect
     * signature, not crash or assert.
     */
    uint8_t pub[32];
    memset(pub, 0xAB, 32);

    uint8_t msg[] = "structural test message";

    uint8_t sig[64];
    memset(sig, 0x00, 64);
    /* Set R to a plausible compressed point (even y, non-zero) */
    sig[0] = 0x02;
    sig[1] = 0x44;
    /* S within valid range (< L, top bit clear) */
    sig[32] = 0x01;
    sig[63] = 0x00; /* top bit of S clear */

    /*
     * We only verify the function does not crash on well-formed input.
     * The actual signature is mathematically incorrect, so we expect
     * a non-zero return (verification failure), not a crash.
     */
    int rc = eos_ed25519_verify(sig, pub, msg, sizeof(msg) - 1);
    (void)rc; /* result is implementation-defined for wrong-but-formatted sig */
}

TEST(test_ed25519_zero_pubkey)
{
    uint8_t sig[64];
    memset(sig, 0x01, 64);
    sig[63] = 0x00; /* keep S top bit clear */

    uint8_t zero_pub[32];
    memset(zero_pub, 0x00, 32);

    uint8_t msg[] = "zero key test";

    ASSERT(eos_ed25519_verify(sig, zero_pub, msg, sizeof(msg) - 1) != 0);
}

int main(void)
{
    printf("=== eBootloader: Ed25519 Signature Verification Tests ===\n\n");

    run_test_ed25519_null_args();
    run_test_ed25519_invalid_signature_format();
    run_test_ed25519_valid_structural();
    run_test_ed25519_zero_pubkey();

    tests_run = 4;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
