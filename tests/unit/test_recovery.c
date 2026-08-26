// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC 15288:2023

/**
 * @file test_recovery.c
 * @brief Host tests for UART recovery write range checks
 */

#include "eos_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "eos_boot_log.h"

/* Mock implementations for unresolved symbols */
void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail) { (void)event; (void)slot; (void)detail; }
int eos_boot_log_read(uint32_t index, eos_boot_log_entry_t *out) { (void)index; (void)out; return EOS_OK; }
uint32_t eos_boot_log_get_head(void) { return 0; }

extern int eos_recovery_write_in_range(uint32_t base, uint32_t slot_size,
                                       uint32_t offset, uint16_t len);

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

TEST(test_write_fits_at_start)
{
    ASSERT(eos_recovery_write_in_range(0x08004000u, 0x8000u, 0, 256) == EOS_OK);
}

TEST(test_write_fits_at_end)
{
    ASSERT(eos_recovery_write_in_range(0x08004000u, 0x8000u, 0x8000u - 64, 64) == EOS_OK);
}

TEST(test_write_rejects_past_slot_end)
{
    ASSERT(eos_recovery_write_in_range(0x08004000u, 0x8000u, 0x7FF0u, 32) == EOS_ERR_INVALID);
}

TEST(test_write_rejects_zero_length)
{
    ASSERT(eos_recovery_write_in_range(0x08004000u, 0x8000u, 0, 0) == EOS_ERR_INVALID);
}

TEST(test_write_rejects_empty_slot)
{
    ASSERT(eos_recovery_write_in_range(0x08004000u, 0, 0, 16) == EOS_ERR_INVALID);
}

TEST(test_write_rejects_zero_base)
{
    ASSERT(eos_recovery_write_in_range(0, 0x8000u, 0, 16) == EOS_ERR_INVALID);
}

TEST(test_write_rejects_len_larger_than_slot)
{
    ASSERT(eos_recovery_write_in_range(0x08004000u, 128, 0, 256) == EOS_ERR_INVALID);
}

TEST(test_write_rejects_base_offset_wrap)
{
    /* offset + base would wrap a uint32_t add used as the flash address. */
    ASSERT(eos_recovery_write_in_range(0xFFFFFFF0u, 0x1000u, 0x20u, 16) == EOS_ERR_INVALID);
}

int main(void)
{
    printf("=== eBootloader: Recovery Write Range Tests ===\n\n");
    run_test_write_fits_at_start();
    run_test_write_fits_at_end();
    run_test_write_rejects_past_slot_end();
    run_test_write_rejects_zero_length();
    run_test_write_rejects_empty_slot();
    run_test_write_rejects_zero_base();
    run_test_write_rejects_len_larger_than_slot();
    run_test_write_rejects_base_offset_wrap();
    tests_run = 8;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
