// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_stm32f4_bootloader_replace.c
 * @brief STM32F4 bootloader replacement: WRP unprotect + ops wiring (ADR-022).
 *
 * The replacement executor lifts the bootloader region's hardware write
 * protection before copying the new stage-1 image, through the HAL's
 * flash_unprotect_bootloader op. On STM32F4 that is the reverse of the
 * WRP program path (#190): set the nWRP bits for sectors 0-3 in
 * FLASH_OPTCR, using the same unlock / write / launch / read-back
 * sequence as the protect call.
 *
 * Same host-redirect harness as the protect test: board_stm32f4.c is
 * compiled on host with the OTP window and the flash registers at RAM
 * buffers, so the real option-byte program path runs without hardware.
 * The flash ops are not exercised: they address real flash, and the
 * host has none.
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

TEST(test_board_ops_wires_replacement)
{
    const eos_board_ops_t *ops = board_get_ops();
    ASSERT(ops != NULL);
    ASSERT(ops->flash_unprotect_bootloader != NULL);
    ASSERT(ops->flash_protect_bootloader != NULL);
    ASSERT(ops->flash_protect_status != NULL);
    /* The replacement gate needs the board's identity: two boards can
     * share an address map, and the TLV target is what distinguishes
     * them. */
    ASSERT(ops->board_id != NULL);
    ASSERT(strcmp(ops->board_id, "stm32f4") == 0);
    /* WRP takes effect at the next reset on STM32F4 (RM0090 §3.9), so
     * the executor must reset between lifting protection and copying. */
    ASSERT(ops->bootloader_protect_needs_reset == true);
}

/* ---- Unprotect sets the nWRP bits for sectors 0-3 ---- */

TEST(test_unprotect_sets_nwrp_for_bootloader_sectors)
{
    reset_fixture();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);

    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_OK);

    /* The bootloader sectors' nWRP bits are set, and the protection
     * is visible through the status op. */
    ASSERT((optcr() & BOOTLOADER_NWRP_MASK) == BOOTLOADER_NWRP_MASK);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);

    /* Every other option bit is preserved: the nWRP bits for sectors
     * 4-11 above the bootloader field, and the control bits below the
     * RDP byte except OPTLOCK/OPTSTRT, which the program sequence
     * itself drives. */
    ASSERT((optcr() & 0xFFF00000u) == 0x0FF00000u);
    ASSERT((optcr() & 0xFCu) == (OPTCR_RESET & 0xFCu));
}

TEST(test_unprotect_never_touches_the_rdp_level)
{
    reset_fixture();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);

    /* RDP level 1 (debug locked): lifting WRP must leave it. */
    set_rdp(0x00);
    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_OK);
    ASSERT(((optcr() >> 8) & 0xFFu) == 0x00);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);
}

TEST(test_unprotect_leaves_other_sectors_protected)
{
    reset_fixture();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    /* Protect sector 5 as well: a sector outside the bootloader region. */
    eboot_test_flash_regs[FLASH_OPTCR_IDX] &= ~0x00200000u;

    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_OK);

    /* Sectors 0-3 are writable again; sector 5 is still protected. */
    ASSERT((optcr() & BOOTLOADER_NWRP_MASK) == BOOTLOADER_NWRP_MASK);
    ASSERT((optcr() & 0x00200000u) == 0);
}

TEST(test_unprotect_is_idempotent_when_already_unprotected)
{
    reset_fixture();
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);

    uint32_t before = optcr();
    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_OK);
    ASSERT(optcr() == before);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);
}

/* RDP level 2 permanently locks the option bytes (RM0090 §3.9), so a
 * device that is not already unprotected cannot be programmed: the
 * unprotect call fails closed, the RDP byte is untouched, and the
 * region stays protected. */
TEST(test_unprotect_fails_closed_at_rdp_level_2)
{
    reset_fixture();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    set_rdp(0xCC);

    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_ERR_FLASH);
    ASSERT(((optcr() >> 8) & 0xFFu) == 0xCC);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);
}

/* The level-2 exception is a device whose WRP is already lifted:
 * nothing needs programming, so the call succeeds without touching
 * the option bytes at all. */
TEST(test_unprotect_is_idempotent_at_rdp_level_2_when_already_unprotected)
{
    reset_fixture();
    set_rdp(0xCC);

    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_OK);
    ASSERT(((optcr() >> 8) & 0xFFu) == 0xCC);
    ASSERT((optcr() & BOOTLOADER_NWRP_MASK) == BOOTLOADER_NWRP_MASK);
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_UNPROTECTED);
}

/* A program whose error flags are set is reported, not passed. */
TEST(test_unprotect_reports_a_failed_program)
{
    reset_fixture();
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    eboot_test_flash_regs[FLASH_SR_IDX] |= 0x10; /* programming error */

    ASSERT(eos_hal_flash_unprotect_bootloader() != EOS_OK);
}

/* Protect -> unprotect -> protect round-trips the option bytes: the
 * executor lifts WRP, copies, and re-applies it on every replacement,
 * so the pair must compose without drifting the other option bits. */
TEST(test_protect_unprotect_protect_round_trips)
{
    reset_fixture();

    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);
    ASSERT(eos_hal_flash_unprotect_bootloader() == EOS_OK);
    ASSERT(eos_hal_flash_protect_bootloader() == EOS_OK);

    /* The bootloader sectors are protected again, and every other
     * option bit is preserved -- except OPTLOCK/OPTSTRT, which the
     * program sequence itself drives. */
    ASSERT((optcr() & BOOTLOADER_NWRP_MASK) == 0);
    ASSERT((optcr() & 0xFFF00000u) == 0x0FF00000u);
    ASSERT((optcr() & 0xFCu) == (OPTCR_RESET & 0xFCu));
    ASSERT(read_status() == EOS_BOOTLOADER_PROTECT_PROTECTED);
}

int main(void)
{
    printf("STM32F4 bootloader replacement (WRP unprotect)\n\n");

    run_test_board_ops_wires_replacement();
    run_test_unprotect_sets_nwrp_for_bootloader_sectors();
    run_test_unprotect_never_touches_the_rdp_level();
    run_test_unprotect_leaves_other_sectors_protected();
    run_test_unprotect_is_idempotent_when_already_unprotected();
    run_test_unprotect_fails_closed_at_rdp_level_2();
    run_test_unprotect_is_idempotent_at_rdp_level_2_when_already_unprotected();
    run_test_unprotect_reports_a_failed_program();
    run_test_protect_unprotect_protect_round_trips();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
