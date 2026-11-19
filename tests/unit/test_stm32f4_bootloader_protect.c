// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_stm32f4_bootloader_protect.c
 * @brief STM32F4 bootloader self-protection: WRP option bytes.
 *
 * The bootloader region (stage-0 + stage-1, sectors 0-3) has no
 * hardware write protection anywhere in the tree (#190): the only WRP
 * mention is the debug-lock test noting the nWRP field is preserved
 * by the RDP lock. The board now programs the FLASH_OPTCR nWRP bits
 * for sectors 0-3 through the HAL's flash_protect_bootloader op and
 * reports the state through flash_protect_status; the core verifies
 * the protection by read-back before it may return EOS_OK.
 *
 * board_stm32f4.c drives flash registers and is built only by a cross
 * build, so this suite compiles it on host with the OTP window and the
 * flash registers redirected at RAM buffers: EBLDR_TEST_OTP_WINDOW and
 * EBLDR_TEST_FLASH_REGS (see the board file). The tests below run the
 * real WRP program path -- unlock, write OPTCR, launch with OPTSTRT,
 * read the nWRP field back -- not a model of it. The flash ops are
 * not exercised: they address real flash, and the host has none -- but
 * the HAL allow-list sits above the board driver, so a refused write
 * to the bootloader region is observable on host without touching
 * flash, and that refusal is pinned here too.
 *
 * What the RAM model cannot show, and what is therefore pinned at the
 * core layer instead: a program that reports success but leaves the
 * nWRP field unchanged is caught by the read-back in
 * stm32f4_flash_protect_bootloader() itself, and
 * test_bootloader_protect.c pins the core's refusal when the board
 * has no protect op at all. The one lock the RAM model cannot show
 * either -- RDP level 2 freezing the option bytes -- is handled by
 * the code failing closed before any program attempt, so the host
 * tests see the same failure the hardware would.
 */

#include "board_stm32f4.h"
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

/* ---- RAM-backed OTP window and flash registers ----
 * Defined here, declared extern in boards/stm32f4/board_stm32f4.c under the
 * EBLDR_TEST_* defines. FLASH_OPTCR is at offset 0x14, so index 5 of the
 * register array; FLASH_SR is at 0x0C, index 3. */
uint8_t eboot_test_otp_window[STM32F4_OTP_SIZE];
uint32_t eboot_test_flash_regs[8];

#define FLASH_SR_IDX    3
#define FLASH_OPTCR_IDX 5

/* The OPTCR reset value (RM0090 §3.9.7): RDP 0xAA (level 0, unlocked),
 * nWRP all set, BOR level 1, watchdog in hardware mode. */
#define OPTCR_RESET     0x0FFFAAEDu

/* nWRP bits for the bootloader sectors 0-3: OPTCR bits 16-19. */
#define BOOTLOADER_NWRP_MASK 0x000F0000u

static uint32_t optcr(void)
{
    return eboot_test_flash_regs[FLASH_OPTCR_IDX];
}

static void set_rdp(uint32_t level)
{
    eboot_test_flash_regs[FLASH_OPTCR_IDX] =
        (eboot_test_flash_regs[FLASH_OPTCR_IDX] & ~0xFF00u) |
        ((level & 0xFFu) << 8);
}

static void reset_fixture(void)
{
    memset(eboot_test_otp_window, 0xFF, sizeof(eboot_test_otp_window));
    memset(eboot_test_flash_regs, 0, sizeof(eboot_test_flash_regs));
    eboot_test_flash_regs[FLASH_OPTCR_IDX] = OPTCR_RESET;
    eos_hal_init(board_get_ops());
}

static uint32_t read_status(void)
{
    uint32_t status = 0xDEAD;
    if (eos_hal_flash_protect_status(&status) != EOS_OK) return 0xDEAD;
    return status;
}

/* ---- Ops wiring ---- */

TEST(test_board_ops_wires_bootloader_protection)
{
    const eos_board_ops_t *ops = board_get_ops();
    ASSERT(ops != NULL);
    ASSERT(ops->flash_protect_bootloader != NULL);
    ASSERT(ops->flash_protect_status != NULL);
    /* The declared region is stage-0 + stage-1: sectors 0-3. */
    ASSERT(ops->bootloader_addr == STM32F4_BOOTLOADER_ADDR);
    ASSERT(ops->bootloader_addr == 0x08000000u);
    ASSERT(ops->bootloader_size == STM32F4_BOOTLOADER_SIZE);
    ASSERT(ops->bootloader_size == (16u + 48u) * 1024u);
}

/* ---- Status decodes the nWRP field ---- */

TEST(test_status_reports_unprotected_at_reset)
{
    reset_fixture();

    /* nWRP all set: no sector protected. */
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);
    ASSERT(eos_hal_flash_protect_status(NULL) == EOS_ERR_INVALID);
}

TEST(test_status_reports_partial_protection_as_unprotected)
{
    reset_fixture();

    /* Only sectors 0-1 protected: the bootloader is one writable
     * sector away from being overwritten, so this is unprotected. */
    eboot_test_flash_regs[FLASH_OPTCR_IDX] &= ~0x00030000u;
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);

    /* All four bootloader sectors protected, the rest writable. */
    eboot_test_flash_regs[FLASH_OPTCR_IDX] &= ~BOOTLOADER_NWRP_MASK;
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);
}

/* ---- Protect programs the nWRP bits for sectors 0-3 ---- */

TEST(test_protect_programs_nwrp_for_bootloader_sectors)
{
    reset_fixture();
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);

    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);

    /* The bootloader sectors' nWRP bits are clear, and the protection
     * is visible through the status op. */
    ASSERT((optcr() & BOOTLOADER_NWRP_MASK) == 0);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);

    /* Every other option bit is preserved: the nWRP bits for sectors
     * 4-11 above the bootloader field, and the control bits below the
     * RDP byte except OPTLOCK/OPTSTRT, which the protect sequence
     * itself drives. */
    ASSERT((optcr() & 0xFFF00000u) == 0x0FF00000u);
    ASSERT((optcr() & 0xFCu) == (OPTCR_RESET & 0xFCu));
}

TEST(test_protect_never_touches_the_rdp_level)
{
    reset_fixture();

    /* RDP level 1 (debug locked): WRP programming must leave it. */
    set_rdp(0x00);
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    ASSERT(((optcr() >> 8) & 0xFFu) == 0x00);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);
}

/* RDP level 2 permanently locks the option bytes (RM0090 §3.9), so a
 * device that is not already protected cannot be programmed: the
 * protect call fails closed, the RDP byte is untouched, and the
 * region stays unprotected -- it must not report success. */
TEST(test_protect_fails_closed_at_rdp_level_2)
{
    reset_fixture();
    set_rdp(0xCC);

    ASSERT(eos_hal_flash_protect_bootloader() == EOS_ERR_FLASH);
    ASSERT(((optcr() >> 8) & 0xFFu) == 0xCC);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);
}

/* The level-2 exception is a device whose WRP is already programmed:
 * nothing needs programming, so the call succeeds without touching
 * the option bytes at all. */
TEST(test_protect_is_idempotent_at_rdp_level_2_when_already_protected)
{
    reset_fixture();
    set_rdp(0xCC);
    eboot_test_flash_regs[FLASH_OPTCR_IDX] &= ~BOOTLOADER_NWRP_MASK;

    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    ASSERT(((optcr() >> 8) & 0xFFu) == 0xCC);
    ASSERT((optcr() & BOOTLOADER_NWRP_MASK) == 0);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);
}

TEST(test_protect_is_idempotent_when_already_protected)
{
    reset_fixture();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);

    uint32_t protected = optcr();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    ASSERT(optcr() == protected);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);
}

/* A program whose error flags are set is reported, not passed: the
 * nWRP field may look right in the register while the option bytes did
 * not actually program. */
TEST(test_protect_reports_a_failed_program)
{
    reset_fixture();
    eboot_test_flash_regs[FLASH_SR_IDX] |= 0x10; /* programming error */

    ASSERT(eos_hal_flash_protect_bootloader() != EOS_OK);
}

/* ---- The HAL allow-list engages on the real STM32F4 ops ---- */

TEST(test_hal_refuses_bootloader_writes_on_real_stm32f4_ops)
{
    reset_fixture();
    uint8_t buf[16] = {0};

    /* The guard sits above the board driver, so these return before
     * any real flash address is touched -- safe to assert on host. */
    ASSERT(eos_hal_flash_write(STM32F4_BOOTLOADER_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_write(STM32F4_STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_erase(STM32F4_BOOTLOADER_ADDR, 0x4000) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_erase(STM32F4_STAGE1_ADDR, 0x4000) == EOS_ERR_WRITE_PROTECTED);
}

int main(void)
{
    printf("STM32F4 bootloader self-protection (WRP option bytes)\n\n");

    run_test_board_ops_wires_bootloader_protection();
    run_test_status_reports_unprotected_at_reset();
    run_test_status_reports_partial_protection_as_unprotected();
    run_test_protect_programs_nwrp_for_bootloader_sectors();
    run_test_protect_never_touches_the_rdp_level();
    run_test_protect_fails_closed_at_rdp_level_2();
    run_test_protect_is_idempotent_at_rdp_level_2_when_already_protected();
    run_test_protect_is_idempotent_when_already_protected();
    run_test_protect_reports_a_failed_program();
    run_test_hal_refuses_bootloader_writes_on_real_stm32f4_ops();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
