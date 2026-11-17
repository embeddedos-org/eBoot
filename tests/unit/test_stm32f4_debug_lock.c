// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_stm32f4_debug_lock.c
 * @brief STM32F4 debug lock: RDP option bytes, not a user OTP byte.
 *
 * STM32F4 debug protection is the RDP level in the FLASH_OPTCR option
 * bytes (RM0090 §3.9). secure_boot used to write 0xFF to OTP 0x1F0 and
 * report the port locked -- a no-op on erased OTP (#184). The board now
 * programs RDP level 1 (0x00) through the HAL's debug_lock op and
 * reports the programmed level through debug_status; the core verifies
 * the lock by read-back before it may return EOS_OK.
 *
 * board_stm32f4.c drives flash registers and is built only by a cross
 * build, so this suite compiles it on host with the OTP window and the
 * flash registers redirected at RAM buffers: EBLDR_TEST_OTP_WINDOW and
 * EBLDR_TEST_FLASH_REGS (see the board file). The tests below run the
 * real RDP program path -- unlock, write OPTCR, launch with OPTSTRT,
 * read the RDP field back -- not a model of it.
 *
 * What the RAM model cannot show, and what is therefore pinned at the
 * core layer instead: a program that reports success but leaves the RDP
 * field unchanged is caught by the read-back in stm32f4_debug_lock()
 * itself, and test_secure_boot_policy.c pins the core's refusal when
 * the status does not confirm the lock.
 */

#include "board_stm32f4.h"
#include "eos_hal.h"
#include "eos_debug_lock.h"
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
    if (eos_hal_debug_status(&status) != EOS_OK) return 0xDEAD;
    return status;
}

/* ---- Ops wiring ---- */

TEST(test_board_ops_wires_debug_lock)
{
    const eos_board_ops_t *ops = board_get_ops();
    ASSERT(ops != NULL);
    ASSERT(ops->debug_lock != NULL);
    ASSERT(ops->debug_status != NULL);
}

/* ---- Status decodes the RDP level ---- */

TEST(test_status_reports_each_rdp_level)
{
    reset_fixture();

    set_rdp(0xAA);
    ASSERT(read_status() == EOS_DEBUG_STATUS_UNLOCKED);

    set_rdp(0x00);
    ASSERT(read_status() == EOS_DEBUG_STATUS_LOCKED);

    /* Any value other than 0xAA and 0xCC is level 1 (RM0090 §3.9.1),
     * including the erased value 0xFF. */
    set_rdp(0xFF);
    ASSERT(read_status() == EOS_DEBUG_STATUS_LOCKED);
    set_rdp(0x55);
    ASSERT(read_status() == EOS_DEBUG_STATUS_LOCKED);

    set_rdp(0xCC);
    ASSERT(read_status() == EOS_DEBUG_STATUS_PERMANENT);

    ASSERT(eos_hal_debug_status(NULL) == EOS_ERR_INVALID);
}

/* ---- Lock programs RDP level 1 ---- */

TEST(test_lock_programs_rdp_level_1)
{
    reset_fixture();
    ASSERT(read_status() == EOS_DEBUG_STATUS_UNLOCKED);

    ASSERT(eos_hal_debug_lock() == EOS_OK);

    /* The RDP field reads back as level 1, and the lock is visible
     * through the status op. */
    ASSERT(((optcr() >> 8) & 0xFFu) == 0x00);
    ASSERT(read_status() == EOS_DEBUG_STATUS_LOCKED);

    /* Every other option bit is preserved: the nWRP field above the RDP
     * byte, and the control bits below it except OPTLOCK/OPTSTRT, which
     * the lock sequence itself drives. */
    ASSERT((optcr() & 0xFFFF0000u) == 0x0FFF0000u);
    ASSERT((optcr() & 0xFCu) == (OPTCR_RESET & 0xFCu));
}

TEST(test_lock_is_idempotent_when_already_locked)
{
    reset_fixture();
    ASSERT(eos_hal_debug_lock() == EOS_OK);

    uint32_t locked = optcr();
    ASSERT(eos_hal_debug_lock() == EOS_OK);
    ASSERT(optcr() == locked);
    ASSERT(read_status() == EOS_DEBUG_STATUS_LOCKED);
}

TEST(test_lock_never_programs_level_2)
{
    reset_fixture();
    ASSERT(eos_hal_debug_lock() == EOS_OK);

    /* Level 2 (0xCC) is irreversible: the lock must never produce it. */
    ASSERT(((optcr() >> 8) & 0xFFu) != 0xCC);
    ASSERT(read_status() != EOS_DEBUG_STATUS_PERMANENT);
}

TEST(test_lock_leaves_a_permanent_device_alone)
{
    reset_fixture();
    set_rdp(0xCC);
    ASSERT(read_status() == EOS_DEBUG_STATUS_PERMANENT);

    uint32_t before = optcr();
    ASSERT(eos_hal_debug_lock() == EOS_OK);
    ASSERT(optcr() == before);
    ASSERT(read_status() == EOS_DEBUG_STATUS_PERMANENT);
}

/* A program whose error flags are set is reported, not passed: the
 * RDP field may look right in the register while the option bytes did
 * not actually program. */
TEST(test_lock_reports_a_failed_program)
{
    reset_fixture();
    eboot_test_flash_regs[FLASH_SR_IDX] |= 0x10; /* programming error */

    ASSERT(eos_hal_debug_lock() != EOS_OK);
}

int main(void)
{
    printf("STM32F4 debug lock (RDP option bytes)\n\n");

    run_test_board_ops_wires_debug_lock();
    run_test_status_reports_each_rdp_level();
    run_test_lock_programs_rdp_level_1();
    run_test_lock_is_idempotent_when_already_locked();
    run_test_lock_never_programs_level_2();
    run_test_lock_leaves_a_permanent_device_alone();
    run_test_lock_reports_a_failed_program();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
