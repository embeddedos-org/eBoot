// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_ecc.c
 * @brief Unit tests for overflow-safe ECC memory range validation
 *
 * Only zero-length check requests are used here. They exercise every range
 * boundary without dereferencing the synthetic 32-bit addresses on the host.
 */

#include "eos_ecc.h"

#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "[FAIL] %s:%d: %s\n",                           \
                    __FILE__, __LINE__, #condition);                          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void test_init_rejects_wrapped_range(void)
{
    eos_ecc_ctx_t ctx;

    CHECK(eos_ecc_init(NULL, 0, 4096) == -1);
    CHECK(eos_ecc_init(&ctx, UINT32_MAX - 0xFFu, 0x100u) == -1);
    CHECK(eos_ecc_init(&ctx, UINT32_MAX - 0xFFu, 0xFFu) == 0);
}

static void test_check_rejects_wrapped_request(void)
{
    eos_ecc_ctx_t ctx;
    CHECK(eos_ecc_init(&ctx, 0x1000u, 0x1000u) == 0);

    /* UINT32_MAX + 2 wrapped to 1 in the old addr + len comparison. len is
     * below one word so a buggy implementation returns without dereferencing. */
    CHECK(eos_ecc_check_region(&ctx, UINT32_MAX, 2) == -1);
}

static void test_check_preserves_boundary_semantics(void)
{
    eos_ecc_ctx_t ctx;
    CHECK(eos_ecc_init(&ctx, 0x1000u, 0x1000u) == 0);

    CHECK(eos_ecc_check_region(NULL, 0x1000u, 0) == -1);
    CHECK(eos_ecc_check_region(&ctx, 0x0FFFu, 0) == -1);
    CHECK(eos_ecc_check_region(&ctx, 0x1000u, 0) == 0);
    CHECK(eos_ecc_check_region(&ctx, 0x2000u, 0) == 0);
    CHECK(eos_ecc_check_region(&ctx, 0x2001u, 0) == -1);
    CHECK(eos_ecc_check_region(&ctx, 0x1FFFu, 2) == -1);
}

int main(void)
{
    test_init_rejects_wrapped_range();
    test_check_rejects_wrapped_request();
    test_check_preserves_boundary_semantics();

    if (failures != 0) {
        fprintf(stderr, "%d ECC test(s) failed\n", failures);
        return 1;
    }

    printf("ECC range validation tests passed\n");
    return 0;
}
