// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_bootloader_protect.c
 * @brief Bootloader self-protection: HAL allow-list + core policy.
 *
 * The bootloader region has no write protection anywhere in the tree
 * (#190): storage.c's write_protect is a whole-device software flag,
 * nothing programs WRP, and eos_hal_flash_write()/erase() forward any
 * address to the board driver. This suite pins the fix at the HAL
 * choke point, with mock board ops (the board-level WRP program path
 * is pinned by tests/unit/test_stm32f4_bootloader_protect.c against
 * the real STM32F4 code):
 *
 *  - any write/erase intersecting the declared bootloader region is
 *    refused with EOS_ERR_WRITE_PROTECTED before the driver is called;
 *  - writes/erases outside the region pass through untouched,
 *    including ranges that end exactly at the region start or start
 *    exactly at the region end;
 *  - a board that declares no region keeps today's behaviour (the
 *    guard cannot protect a region the board will not name);
 *  - the range check is overflow-safe: a range that wraps uint32_t
 *    cannot smuggle its tail into the region;
 *  - reads are unaffected, and a missing board still fails the way
 *    it always did;
 *  - eos_bootloader_protect_apply() reports EOS_ERR_NOT_SUPPORTED --
 *    never a silent success -- when the board declares a region but
 *    provides no protect op (fail closed where protection is claimed
 *    but cannot be applied).
 */

#include "eos_bootloader_protect.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("  %-58s ", #name); \
        tests_run++; \
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

/* ---- Mock flash backend (STM32F4 geometry) ---- */

#define FLASH_BASE      0x08000000u
#define FLASH_SIZE      (1024u * 1024u)
#define BOOTLOADER_ADDR FLASH_BASE
#define BOOTLOADER_SIZE (64u * 1024u)   /* stage-0 16K + stage-1 48K */
#define SLOT_A_ADDR     0x08020000u
#define BOOTCTL_ADDR    0x080C0000u
#define LOG_ADDR        0x08010000u

typedef struct {
    uint8_t memory[FLASH_SIZE];
    int write_called;
    int erase_called;
    int protect_called;
    int protect_rc;
    uint32_t protect_status;
    int status_rc;
} mock_ctx_t;

static mock_ctx_t g_ctx;

static int mock_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr < FLASH_BASE || (uint64_t)addr + len > (uint64_t)FLASH_BASE + FLASH_SIZE)
        return EOS_ERR_INVALID;
    memcpy(buf, &g_ctx.memory[addr - FLASH_BASE], len);
    return EOS_OK;
}

static int mock_flash_write(uint32_t addr, const void *buf, size_t len)
{
    g_ctx.write_called++;
    if (addr < FLASH_BASE || (uint64_t)addr + len > (uint64_t)FLASH_BASE + FLASH_SIZE)
        return EOS_ERR_INVALID;
    memcpy(&g_ctx.memory[addr - FLASH_BASE], buf, len);
    return EOS_OK;
}

static int mock_flash_erase(uint32_t addr, size_t len)
{
    g_ctx.erase_called++;
    if (addr < FLASH_BASE || (uint64_t)addr + len > (uint64_t)FLASH_BASE + FLASH_SIZE)
        return EOS_ERR_INVALID;
    memset(&g_ctx.memory[addr - FLASH_BASE], 0xFF, len);
    return EOS_OK;
}

static int mock_protect(void)
{
    g_ctx.protect_called++;
    return g_ctx.protect_rc;
}

static int mock_protect_status(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;
    *status = g_ctx.protect_status;
    return g_ctx.status_rc;
}

static eos_board_ops_t mock_ops = {
    .flash_base      = FLASH_BASE,
    .flash_size      = FLASH_SIZE,
    .slot_a_addr     = SLOT_A_ADDR,
    .bootloader_addr = BOOTLOADER_ADDR,
    .bootloader_size = BOOTLOADER_SIZE,
    .flash_read      = mock_flash_read,
    .flash_write     = mock_flash_write,
    .flash_erase     = mock_flash_erase,
    .flash_protect_bootloader = mock_protect,
    .flash_protect_status     = mock_protect_status,
};

static void reset_fixture(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.protect_rc = EOS_OK;
    g_ctx.status_rc = EOS_OK;
    g_ctx.protect_status = EOS_BOOTLOADER_PROTECT_UNPROTECTED;
    eos_hal_init(&mock_ops);
}

static uint8_t g_buf[64];

/* ---- Allow-list: writes into the region are refused ---- */

TEST(test_write_inside_bootloader_region_is_refused)
{
    reset_fixture();

    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR, g_buf, 16) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR + 0x8000, g_buf, 16) == EOS_ERR_WRITE_PROTECTED);
    /* Last byte of the region. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR + BOOTLOADER_SIZE - 1, g_buf, 1) == EOS_ERR_WRITE_PROTECTED);
    /* The whole region at once. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR, g_buf, BOOTLOADER_SIZE) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(g_ctx.write_called == 0);
}

TEST(test_write_straddling_the_region_is_refused)
{
    reset_fixture();

    /* Starts before the region, ends inside it. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR - 16, g_buf, 32) == EOS_ERR_WRITE_PROTECTED);
    /* Starts inside the region, ends past it. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR + BOOTLOADER_SIZE - 16, g_buf, 32) == EOS_ERR_WRITE_PROTECTED);
    /* Starts before and ends past the region entirely. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR - 16, g_buf, BOOTLOADER_SIZE + 32) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(g_ctx.write_called == 0);
}

TEST(test_erase_intersecting_the_region_is_refused)
{
    reset_fixture();

    ASSERT(eos_hal_flash_erase(BOOTLOADER_ADDR, 0x4000) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_erase(BOOTLOADER_ADDR - 0x4000, 0x8000) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_erase(BOOTLOADER_ADDR + BOOTLOADER_SIZE - 0x4000, 0x8000) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(g_ctx.erase_called == 0);
}

TEST(test_write_wrapping_uint32_is_refused)
{
    reset_fixture();

    /* addr + len wraps past 2^32, and the tail that re-enters at address
     * 0 reaches into the region: (0xFFFFFF00 + 0x08000200) mod 2^32 is
     * 0x08000100, inside [0x08000000, 0x08010000). A check that only
     * looked at the linear interval would pass this: the linear end
     * (0x108000100) sits entirely above the region. */
    ASSERT(eos_hal_flash_write(0xFFFFFF00u, g_buf, 0x08000200u) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_erase(0xFFFFFF00u, 0x08000200u) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(g_ctx.write_called == 0);
    ASSERT(g_ctx.erase_called == 0);

    /* Control: a wrapping range whose tail stops short of the region
     * ([0, 0x100)) is not the guard's business -- it passes through
     * and the backend refuses it for leaving the flash array. */
    ASSERT(eos_hal_flash_write(0xFFFFFF00u, g_buf, 0x200) == EOS_ERR_INVALID);
    ASSERT(g_ctx.write_called == 1);
}

/* ---- Allow-list: writes outside the region pass through ---- */

TEST(test_writes_outside_the_region_pass_through)
{
    reset_fixture();

    /* Slot A, boot control, boot log: the legitimate writers. */
    ASSERT(eos_hal_flash_write(SLOT_A_ADDR, g_buf, 16) == EOS_OK);
    ASSERT(eos_hal_flash_write(BOOTCTL_ADDR, g_buf, 16) == EOS_OK);
    ASSERT(eos_hal_flash_write(LOG_ADDR, g_buf, 16) == EOS_OK);
    ASSERT(eos_hal_flash_erase(SLOT_A_ADDR, 0x4000) == EOS_OK);
    ASSERT(g_ctx.write_called == 3);
    ASSERT(g_ctx.erase_called == 1);

    /* The written bytes really landed (the guard did not swallow them).
     * The boot-control write is checked, not the slot-A one: the erase
     * below wipes slot A by design. */
    ASSERT(g_ctx.memory[BOOTCTL_ADDR - FLASH_BASE] == g_buf[0]);
}

TEST(test_ranges_touching_the_region_boundary_pass_through)
{
    reset_fixture();

    /* Ends exactly at the region start: [addr, region_start). */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR - 16, g_buf, 16) == EOS_ERR_INVALID);
    /* The mock refuses it for leaving the flash array, which proves the
     * guard let it through instead of refusing it as protected. */
    ASSERT(g_ctx.write_called == 1);

    /* Starts exactly at the region end: the boot log sector. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR + BOOTLOADER_SIZE, g_buf, 16) == EOS_OK);
    ASSERT(g_ctx.write_called == 2);
}

TEST(test_reads_of_the_region_are_unaffected)
{
    reset_fixture();

    uint8_t buf[16] = {0};
    ASSERT(eos_hal_flash_read(BOOTLOADER_ADDR, buf, 16) == EOS_OK);
    ASSERT(eos_hal_flash_read(BOOTLOADER_ADDR + BOOTLOADER_SIZE - 16, buf, 16) == EOS_OK);
}

/* ---- No declared region: today's behaviour is preserved ---- */

TEST(test_undeclared_region_is_not_guarded)
{
    reset_fixture();
    mock_ops.bootloader_addr = 0;
    mock_ops.bootloader_size = 0;
    eos_hal_init(&mock_ops);

    /* The guard cannot protect a region the board will not name. */
    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR, g_buf, 16) == EOS_OK);
    ASSERT(eos_hal_flash_erase(BOOTLOADER_ADDR, 0x4000) == EOS_OK);
    ASSERT(g_ctx.write_called == 1);
    ASSERT(g_ctx.erase_called == 1);

    mock_ops.bootloader_addr = BOOTLOADER_ADDR;
    mock_ops.bootloader_size = BOOTLOADER_SIZE;
}

TEST(test_missing_board_still_fails_the_old_way)
{
    eos_hal_init(NULL);

    ASSERT(eos_hal_flash_write(BOOTLOADER_ADDR, g_buf, 16) == EOS_ERR_GENERIC);
    ASSERT(eos_hal_flash_erase(BOOTLOADER_ADDR, 0x4000) == EOS_ERR_GENERIC);
}

/* ---- Core policy: apply / verify ---- */

TEST(test_apply_calls_the_board_protect_op)
{
    reset_fixture();

    ASSERT(eos_bootloader_protect_apply() == EOS_OK);
    ASSERT(g_ctx.protect_called == 1);
}

TEST(test_apply_propagates_a_failed_program)
{
    reset_fixture();
    g_ctx.protect_rc = EOS_ERR_FLASH;

    ASSERT(eos_bootloader_protect_apply() == EOS_ERR_FLASH);
    ASSERT(g_ctx.protect_called == 1);
}

TEST(test_apply_fails_closed_without_a_protect_op)
{
    reset_fixture();
    mock_ops.flash_protect_bootloader = NULL;
    eos_hal_init(&mock_ops);

    /* The board declares a region but has no primitive: NOT_SUPPORTED,
     * never a silent success. A production build must treat this as
     * fatal; the HAL allow-list still refuses the region, but the
     * hardware protection is absent. */
    ASSERT(eos_bootloader_protect_apply() == EOS_ERR_NOT_SUPPORTED);
    ASSERT(g_ctx.protect_called == 0);

    mock_ops.flash_protect_bootloader = mock_protect;
}

TEST(test_verify_reports_the_board_status)
{
    reset_fixture();
    uint32_t status = 0xDEAD;

    g_ctx.protect_status = EOS_BOOTLOADER_PROTECT_PROTECTED;
    ASSERT(eos_bootloader_protect_verify(&status) == EOS_OK);
    ASSERT(status == EOS_BOOTLOADER_PROTECT_PROTECTED);

    g_ctx.protect_status = EOS_BOOTLOADER_PROTECT_UNPROTECTED;
    ASSERT(eos_bootloader_protect_verify(&status) == EOS_OK);
    ASSERT(status == EOS_BOOTLOADER_PROTECT_UNPROTECTED);

    ASSERT(eos_bootloader_protect_verify(NULL) == EOS_ERR_INVALID);
}

TEST(test_verify_fails_closed_without_a_status_op)
{
    reset_fixture();
    mock_ops.flash_protect_status = NULL;
    eos_hal_init(&mock_ops);

    uint32_t status = 0xDEAD;
    ASSERT(eos_bootloader_protect_verify(&status) == EOS_ERR_NOT_SUPPORTED);

    mock_ops.flash_protect_status = mock_protect_status;
}

int main(void)
{
    printf("Bootloader self-protection (HAL allow-list + core policy)\n\n");

    run_test_write_inside_bootloader_region_is_refused();
    run_test_write_straddling_the_region_is_refused();
    run_test_erase_intersecting_the_region_is_refused();
    run_test_write_wrapping_uint32_is_refused();
    run_test_writes_outside_the_region_pass_through();
    run_test_ranges_touching_the_region_boundary_pass_through();
    run_test_reads_of_the_region_are_unaffected();
    run_test_undeclared_region_is_not_guarded();
    run_test_missing_board_still_fails_the_old_way();
    run_test_apply_calls_the_board_protect_op();
    run_test_apply_propagates_a_failed_program();
    run_test_apply_fails_closed_without_a_protect_op();
    run_test_verify_reports_the_board_status();
    run_test_verify_fails_closed_without_a_status_op();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
