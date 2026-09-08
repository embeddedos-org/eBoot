// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_qemu_arm64_timer.c
 * @brief Unit tests for QEMU ARM64 generic timer conversion
 */

#include "qemu_arm64_timer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_EQ(expected, actual) do { \
    uint32_t expected_value = (expected); \
    uint32_t actual_value = (actual); \
    if (expected_value != actual_value) { \
        printf("[FAIL] %s:%d: expected %u, got %u\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        exit(1); \
    } \
} while (0)

int main(void)
{
    printf("=== eBootloader: QEMU ARM64 Timer Unit Tests ===\n\n");

    ASSERT_EQ(0U, qemu_arm64_counter_to_ms(0U, 1000000U));
    ASSERT_EQ(1500U, qemu_arm64_counter_to_ms(1500000U, 1000000U));
    ASSERT_EQ(1271310319U,
              qemu_arm64_counter_to_ms(UINT64_MAX, 1000000U));
    ASSERT_EQ(123U,
              qemu_arm64_counter_to_ms(4294967296123ULL, 1000U));

    printf("4/4 tests passed\n");
    return 0;
}
