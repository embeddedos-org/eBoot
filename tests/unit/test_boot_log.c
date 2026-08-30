// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_boot_log.c
 * @brief Unit tests for the boot log subsystem
 *
 * These exercise core/boot_log.c itself. The previous version of this file
 * defined its own eos_boot_log_* functions, so the linker never pulled
 * boot_log.c out of libeboot_core.a and the suite tested only its own stubs.
 * Only the platform below is stubbed: flash and the tick counter.
 */

#include "eos_boot_log.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_passed = 0;

#define ASSERT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "[FAIL] %s:%d: %s\n",                             \
                    __FILE__, __LINE__, #condition);                          \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define RUN(test)                                                             \
    do {                                                                      \
        reset_fixture();                                                      \
        test();                                                               \
        tests_passed++;                                                       \
        printf("[PASS] %s\n", #test);                                         \
    } while (0)

/* ---- Simulated flash sector holding the log ---- */

#define LOG_BASE 0x8000u
#define LOG_BYTES (EOS_BOOT_LOG_MAX * sizeof(eos_boot_log_entry_t))

static uint8_t sim_flash[LOG_BYTES];
static int write_result;
static int erase_result;
static uint32_t erased_addr;
static size_t erased_size;
static uint32_t sim_tick;

static eos_board_ops_t sim_ops;

const eos_board_ops_t *eos_hal_get_ops(void)
{
    return &sim_ops;
}

uint32_t eos_hal_get_tick_ms(void)
{
    return ++sim_tick;
}

static int in_log(uint32_t addr, size_t len)
{
    return addr >= LOG_BASE && (uint64_t)addr + len <= (uint64_t)LOG_BASE + LOG_BYTES;
}

int eos_hal_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!buf || !in_log(addr, len))
        return EOS_ERR_INVALID;
    memcpy(buf, sim_flash + (addr - LOG_BASE), len);
    return EOS_OK;
}

int eos_hal_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!buf || !in_log(addr, len))
        return EOS_ERR_INVALID;
    if (write_result != EOS_OK)
        return write_result;
    memcpy(sim_flash + (addr - LOG_BASE), buf, len);
    return EOS_OK;
}

int eos_hal_flash_erase(uint32_t addr, size_t len)
{
    erased_addr = addr;
    erased_size = len;
    if (erase_result != EOS_OK)
        return erase_result;
    if (!in_log(addr, len))
        return EOS_ERR_INVALID;
    memset(sim_flash + (addr - LOG_BASE), 0xFF, len);
    return EOS_OK;
}

static void reset_fixture(void)
{
    memset(&sim_ops, 0, sizeof(sim_ops));
    sim_ops.log_addr = LOG_BASE;
    memset(sim_flash, 0xFF, sizeof(sim_flash));
    write_result = EOS_OK;
    erase_result = EOS_OK;
    erased_addr = 0;
    erased_size = 0;
    sim_tick = 0;
}

/* ---- Tests ---- */

/* stage0 reads the head out of the boot control block and hands it to
 * eos_boot_log_init(). Appending before that would scribble over entry 0 of
 * whatever the previous boot wrote, so it has to be a no-op. */
static void test_append_before_init_is_ignored(void)
{
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);

    for (size_t i = 0; i < LOG_BYTES; i++)
        ASSERT(sim_flash[i] == 0xFF);
}

static void test_append_writes_at_head_and_advances(void)
{
    eos_boot_log_init(0);
    ASSERT(eos_boot_log_get_head() == 0);

    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 100);
    ASSERT(eos_boot_log_get_head() == 1);

    eos_boot_log_append(EOS_LOG_ROLLBACK, EOS_SLOT_B, 200);
    ASSERT(eos_boot_log_get_head() == 2);

    eos_boot_log_entry_t entry;
    ASSERT(eos_boot_log_read(0, &entry) == EOS_OK);
    ASSERT(entry.event == EOS_LOG_BOOT_START);
    ASSERT(entry.slot == EOS_SLOT_A);
    ASSERT(entry.detail == 100);

    ASSERT(eos_boot_log_read(1, &entry) == EOS_OK);
    ASSERT(entry.event == EOS_LOG_ROLLBACK);
    ASSERT(entry.slot == EOS_SLOT_B);
    ASSERT(entry.detail == 200);
}

/* The head is persisted across resets, so init() must resume where the last
 * boot stopped rather than overwriting from zero. */
static void test_init_resumes_from_persisted_head(void)
{
    eos_boot_log_init(5);
    ASSERT(eos_boot_log_get_head() == 5);

    eos_boot_log_append(EOS_LOG_CONFIRM, EOS_SLOT_A, 42);

    eos_boot_log_entry_t entry;
    ASSERT(eos_boot_log_read(5, &entry) == EOS_OK);
    ASSERT(entry.event == EOS_LOG_CONFIRM);
    ASSERT(eos_boot_log_read(0, &entry) == EOS_OK);
    ASSERT(entry.event == 0xFFFFFFFFu); /* untouched erased flash */
}

/* A corrupt boot control block can hand back any 32-bit value; it must wrap
 * into the ring instead of indexing past the log sector. */
static void test_init_wraps_out_of_range_head(void)
{
    eos_boot_log_init(EOS_BOOT_LOG_MAX + 3);
    ASSERT(eos_boot_log_get_head() == 3);

    eos_boot_log_init(0xFFFFFFFFu);
    ASSERT(eos_boot_log_get_head() < EOS_BOOT_LOG_MAX);
}

static void test_head_wraps_at_end_of_ring(void)
{
    eos_boot_log_init(EOS_BOOT_LOG_MAX - 1);

    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 1);
    ASSERT(eos_boot_log_get_head() == 0);

    eos_boot_log_append(EOS_LOG_BOOT_FAIL, EOS_SLOT_A, 2);
    ASSERT(eos_boot_log_get_head() == 1);

    eos_boot_log_entry_t entry;
    ASSERT(eos_boot_log_read(EOS_BOOT_LOG_MAX - 1, &entry) == EOS_OK);
    ASSERT(entry.detail == 1);
    ASSERT(eos_boot_log_read(0, &entry) == EOS_OK);
    ASSERT(entry.detail == 2);
}

static void test_entries_are_timestamped_in_order(void)
{
    eos_boot_log_init(0);
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_IMAGE_VALID, EOS_SLOT_A, 0);

    eos_boot_log_entry_t first, second;
    ASSERT(eos_boot_log_read(0, &first) == EOS_OK);
    ASSERT(eos_boot_log_read(1, &second) == EOS_OK);
    ASSERT(second.timestamp > first.timestamp);
}

static void test_read_rejects_bad_arguments(void)
{
    eos_boot_log_init(0);
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);

    eos_boot_log_entry_t entry;
    ASSERT(eos_boot_log_read(0, NULL) == EOS_ERR_INVALID);
    ASSERT(eos_boot_log_read(EOS_BOOT_LOG_MAX, &entry) == EOS_ERR_INVALID);
    ASSERT(eos_boot_log_read(0xFFFFFFFFu, &entry) == EOS_ERR_INVALID);
}

static void test_clear_erases_the_sector_and_resets_head(void)
{
    eos_boot_log_init(0);
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_B, 0);
    ASSERT(eos_boot_log_get_head() == 2);

    ASSERT(eos_boot_log_clear() == EOS_OK);
    ASSERT(eos_boot_log_get_head() == 0);
    ASSERT(erased_addr == LOG_BASE);
    ASSERT(erased_size == LOG_BYTES);

    for (size_t i = 0; i < LOG_BYTES; i++)
        ASSERT(sim_flash[i] == 0xFF);
}

/* A failed erase must not reset the head: reporting success would let the next
 * boot append over entries that are still there. */
static void test_clear_reports_erase_failure(void)
{
    eos_boot_log_init(0);
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);

    erase_result = EOS_ERR_FLASH;
    ASSERT(eos_boot_log_clear() == EOS_ERR_FLASH);
    ASSERT(eos_boot_log_get_head() == 1);
}

static void test_entry_layout_is_stable(void)
{
    /* The log is parsed by host tooling and by application firmware through
     * eos_fw_read_boot_log(), so the on-flash layout is ABI. */
    ASSERT(sizeof(eos_boot_log_entry_t) == 16);
    ASSERT(EOS_BOOT_LOG_MAX == 32);
    ASSERT(EOS_BOOT_LOG_SECTOR_SIZE == 4096);
    ASSERT(LOG_BYTES <= EOS_BOOT_LOG_SECTOR_SIZE);
}

int main(void)
{
    printf("=== eBootloader Boot Log Tests ===\n");
    RUN(test_append_before_init_is_ignored);
    RUN(test_append_writes_at_head_and_advances);
    RUN(test_init_resumes_from_persisted_head);
    RUN(test_init_wraps_out_of_range_head);
    RUN(test_head_wraps_at_end_of_ring);
    RUN(test_entries_are_timestamped_in_order);
    RUN(test_read_rejects_bad_arguments);
    RUN(test_clear_erases_the_sector_and_resets_head);
    RUN(test_clear_reports_erase_failure);
    RUN(test_entry_layout_is_stable);
    printf("\n%d/10 tests passed\n", tests_passed);
    return 0;
}
