// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_stm32f4_otp_counter.c
 * @brief STM32F4 OTP-backed monotonic counter: the device anti-rollback floor.
 *
 * STM32F4 is the first board with a monotonic counter. Its floor is a unary
 * fuse bitmap in the security_version OTP field (offset 0x144, four bytes):
 * the counter is the number of cleared bits, each increment burns exactly
 * one fuse, and the field is the counter's own -- never the recovery secret
 * at 0x180 (#159).
 *
 * board_stm32f4.c drives flash registers and is built only by a cross
 * build, so this suite compiles it on host with the OTP window and the
 * flash registers redirected at RAM buffers: EBLDR_TEST_OTP_WINDOW and
 * EBLDR_TEST_FLASH_REGS (see the board file). The tests below run the real
 * burn path -- otp_write programs through the flash-interface sequence,
 * monotonic_increment reads the count back -- not a model of it. The two
 * operations this suite deliberately does not call are the flash ops: they
 * address real flash, and the host has none.
 *
 * One branch is pinned elsewhere, not here: monotonic_increment's read-back
 * check -- a burn that reports success but leaves the count unchanged --
 * cannot be made to happen against a RAM window, where a write always
 * lands. The behaviour it guards (commit detects a silent fuse failure) is
 * pinned at the commit layer by test_rollback.c's
 * test_commit_detects_silent_fuse_failure, which drives a mock increment
 * that lies. This suite pins the success path (each burn advances the count
 * by exactly one) and the full-field refusal.
 */

#include "board_stm32f4.h"
#include "eos_hal.h"
#include "eos_rollback.h"
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
 * EBLDR_TEST_* defines. The window starts zeroed (all fuses burned); every
 * test resets it to erased (0xFF) first. */
uint8_t eboot_test_otp_window[STM32F4_OTP_SIZE];
uint32_t eboot_test_flash_regs[8];

#define OTP_COUNTER STM32F4_OTP_COUNTER_OFFSET
#define OTP_FIELD_END (STM32F4_OTP_COUNTER_OFFSET + STM32F4_OTP_COUNTER_SIZE)
#define OTP_RECOVERY_SECRET_OFFSET 0x180
#define OTP_RECOVERY_SECRET_SIZE   32

static void reset_fixture(void)
{
    memset(eboot_test_otp_window, 0xFF, sizeof(eboot_test_otp_window));
    memset(eboot_test_flash_regs, 0, sizeof(eboot_test_flash_regs));
    eos_hal_init(board_get_ops());
    eos_rollback_clear_staged();
}

static uint32_t read_counter(void)
{
    uint32_t value = 0xDEAD;
    if (eos_hal_monotonic_read(&value) != EOS_OK) return 0xDEAD;
    return value;
}

/* ---- Bitmap helpers (pure) ---- */

TEST(test_counter_counts_burned_bits)
{
    static const uint8_t erased[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    ASSERT(stm32f4_otp_counter_count(erased, sizeof(erased)) == 0);

    static const uint8_t one[4] = { 0xFE, 0xFF, 0xFF, 0xFF };
    ASSERT(stm32f4_otp_counter_count(one, sizeof(one)) == 1);

    static const uint8_t mixed[4] = { 0xF0, 0x0F, 0xFF, 0x00 };
    ASSERT(stm32f4_otp_counter_count(mixed, sizeof(mixed)) == 4 + 4 + 0 + 8);

    static const uint8_t full[4] = { 0x00, 0x00, 0x00, 0x00 };
    ASSERT(stm32f4_otp_counter_count(full, sizeof(full)) ==
           STM32F4_OTP_COUNTER_MAX);
}

TEST(test_next_burn_picks_the_lowest_set_bit)
{
    uint32_t byte = 99;
    uint8_t value = 0;

    static const uint8_t erased[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    ASSERT(stm32f4_otp_counter_next_burn(erased, sizeof(erased),
                                         &byte, &value) == 0);
    ASSERT(byte == 0 && value == 0xFE);

    static const uint8_t one_burned[4] = { 0xFE, 0xFF, 0xFF, 0xFF };
    ASSERT(stm32f4_otp_counter_next_burn(one_burned, sizeof(one_burned),
                                         &byte, &value) == 0);
    ASSERT(byte == 0 && value == 0xFC);

    /* A spent byte is skipped: the next bit is the lowest set bit of the
     * first byte that still has one. */
    static const uint8_t first_spent[4] = { 0x00, 0xFF, 0xFF, 0xFF };
    ASSERT(stm32f4_otp_counter_next_burn(first_spent, sizeof(first_spent),
                                         &byte, &value) == 0);
    ASSERT(byte == 1 && value == 0xFE);

    static const uint8_t last_bit[4] = { 0x00, 0x00, 0x00, 0x01 };
    ASSERT(stm32f4_otp_counter_next_burn(last_bit, sizeof(last_bit),
                                         &byte, &value) == 0);
    ASSERT(byte == 3 && value == 0x00);

    static const uint8_t full[4] = { 0x00, 0x00, 0x00, 0x00 };
    ASSERT(stm32f4_otp_counter_next_burn(full, sizeof(full),
                                         &byte, &value) != 0);
}

/* ---- Ops wiring ---- */

TEST(test_board_ops_wires_the_counter)
{
    const eos_board_ops_t *ops = board_get_ops();
    ASSERT(ops != NULL);
    ASSERT(ops->otp_read != NULL);
    ASSERT(ops->otp_write != NULL);
    ASSERT(ops->monotonic_read != NULL);
    ASSERT(ops->monotonic_increment != NULL);
}

/* ---- OTP read/write ---- */

TEST(test_otp_read_rejects_out_of_range)
{
    reset_fixture();
    uint8_t buf[4];

    ASSERT(eos_hal_otp_read(STM32F4_OTP_SIZE, buf, 1) == EOS_ERR_INVALID);
    ASSERT(eos_hal_otp_read(STM32F4_OTP_SIZE - 1, buf, 2) == EOS_ERR_INVALID);
    ASSERT(eos_hal_otp_read(0xFFFFFF00u, buf, 0x200) == EOS_ERR_INVALID);
    ASSERT(eos_hal_otp_read(0, NULL, 1) == EOS_ERR_INVALID);

    /* The last four bytes of the array are readable. */
    ASSERT(eos_hal_otp_read(STM32F4_OTP_SIZE - 4, buf, 4) == EOS_OK);
}

TEST(test_otp_write_programs_only_clearable_bits)
{
    reset_fixture();
    uint8_t buf[2];

    /* Clearing bits works, and the bytes read back are the bytes asked for. */
    static const uint8_t first[2] = { 0x0F, 0xF0 };
    ASSERT(eos_hal_otp_write(0x100, first, sizeof(first)) == EOS_OK);
    ASSERT(eos_hal_otp_read(0x100, buf, sizeof(buf)) == EOS_OK);
    ASSERT(buf[0] == 0x0F && buf[1] == 0xF0);

    /* Setting a bit is impossible in OTP: the whole write is refused
     * before anything is programmed, so the first byte -- which was
     * writable on its own -- is untouched too. */
    static const uint8_t impossible[2] = { 0x00, 0xFF };
    ASSERT(eos_hal_otp_write(0x100, impossible, sizeof(impossible)) ==
           EOS_ERR_INVALID);
    ASSERT(eos_hal_otp_read(0x100, buf, sizeof(buf)) == EOS_OK);
    ASSERT(buf[0] == 0x0F && buf[1] == 0xF0);

    /* Out of range, and a NULL buffer, are refused with nothing written. */
    ASSERT(eos_hal_otp_write(STM32F4_OTP_SIZE - 1, first, 2) ==
           EOS_ERR_INVALID);
    ASSERT(eos_hal_otp_write(0x100, NULL, 1) == EOS_ERR_INVALID);
}

/* ---- Monotonic counter through the HAL ---- */

TEST(test_monotonic_read_counts_the_field)
{
    reset_fixture();
    ASSERT(read_counter() == 0);

    /* Burn one fuse through the raw OTP write; the counter sees it. */
    static const uint8_t one[1] = { 0xFE };
    ASSERT(eos_hal_otp_write(OTP_COUNTER, one, sizeof(one)) == EOS_OK);
    ASSERT(read_counter() == 1);

    static const uint8_t two_more[1] = { 0xF8 };
    ASSERT(eos_hal_otp_write(OTP_COUNTER, two_more, sizeof(two_more)) == EOS_OK);
    ASSERT(read_counter() == 3);
}

TEST(test_monotonic_increment_burns_one_bit_at_a_time)
{
    reset_fixture();

    for (int i = 0; i < 5; i++)
        ASSERT(eos_hal_monotonic_increment() == EOS_OK);
    ASSERT(read_counter() == 5);

    /* Five burns, lowest bit first: byte 0 lost bits 0-4, the rest of the
     * field is still erased. */
    uint8_t field[STM32F4_OTP_COUNTER_SIZE];
    ASSERT(eos_hal_otp_read(OTP_COUNTER, field, sizeof(field)) == EOS_OK);
    ASSERT(field[0] == 0xE0);
    ASSERT(field[1] == 0xFF && field[2] == 0xFF && field[3] == 0xFF);
}

TEST(test_increment_never_touches_anything_but_the_counter_field)
{
    reset_fixture();

    /* Provision the rest of the array with a sentinel, so any write
     * outside the field shows up. The field itself starts erased. */
    memset(eboot_test_otp_window, 0xA5, sizeof(eboot_test_otp_window));
    memset(eboot_test_otp_window + OTP_COUNTER, 0xFF,
           STM32F4_OTP_COUNTER_SIZE);

    for (uint32_t i = 0; i < STM32F4_OTP_COUNTER_MAX; i++)
        ASSERT(eos_hal_monotonic_increment() == EOS_OK);
    ASSERT(read_counter() == STM32F4_OTP_COUNTER_MAX);

    for (uint32_t i = 0; i < STM32F4_OTP_SIZE; i++) {
        if (i >= OTP_COUNTER && i < OTP_FIELD_END)
            continue;
        if (eboot_test_otp_window[i] != 0xA5) {
            printf("\n    OTP byte 0x%03X changed (0x%02X) ",
                   (unsigned)i, eboot_test_otp_window[i]);
            ASSERT(0);
        }
    }

    /* The recovery secret above all: the #159 regression, pinned here. */
    for (uint32_t i = 0; i < OTP_RECOVERY_SECRET_SIZE; i++)
        ASSERT(eboot_test_otp_window[OTP_RECOVERY_SECRET_OFFSET + i] == 0xA5);
}

TEST(test_increment_refuses_a_full_counter)
{
    reset_fixture();

    static const uint8_t full[STM32F4_OTP_COUNTER_SIZE] =
        { 0x00, 0x00, 0x00, 0x00 };
    ASSERT(eos_hal_otp_write(OTP_COUNTER, full, sizeof(full)) == EOS_OK);
    ASSERT(read_counter() == STM32F4_OTP_COUNTER_MAX);

    ASSERT(eos_hal_monotonic_increment() == EOS_ERR_FULL);
    ASSERT(read_counter() == STM32F4_OTP_COUNTER_MAX);
}

/* ---- Commit through the real counter ---- */

TEST(test_commit_raises_the_floor_through_the_real_counter)
{
    reset_fixture();

    eos_rollback_stage(10);
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(read_counter() == 10);

    /* Ten burns: byte 0 spent, byte 1 lost two bits. */
    uint8_t field[STM32F4_OTP_COUNTER_SIZE];
    ASSERT(eos_hal_otp_read(OTP_COUNTER, field, sizeof(field)) == EOS_OK);
    ASSERT(field[0] == 0x00 && field[1] == 0xFC);
    ASSERT(field[2] == 0xFF && field[3] == 0xFF);

    ASSERT(eos_rollback_verify(9) == EOS_ERR_ANTI_ROLLBACK);
    ASSERT(eos_rollback_verify(10) == EOS_OK);
}

TEST(test_commit_refuses_more_than_max_step)
{
    reset_fixture();

    eos_rollback_stage(EOS_ROLLBACK_MAX_STEP + 1);
    ASSERT(eos_rollback_commit() == EOS_ERR_INVALID);

    /* Refused before burning anything. */
    ASSERT(read_counter() == 0);
    uint8_t field[STM32F4_OTP_COUNTER_SIZE];
    ASSERT(eos_hal_otp_read(OTP_COUNTER, field, sizeof(field)) == EOS_OK);
    ASSERT(field[0] == 0xFF && field[1] == 0xFF);
    ASSERT(field[2] == 0xFF && field[3] == 0xFF);
}

TEST(test_commit_fails_when_the_fuse_bank_runs_out)
{
    reset_fixture();

    /* 27 of 32 fuses already burned: three whole bytes and three bits. */
    static const uint8_t spent[STM32F4_OTP_COUNTER_SIZE] =
        { 0x00, 0x00, 0x00, 0xF8 };
    ASSERT(eos_hal_otp_write(OTP_COUNTER, spent, sizeof(spent)) == EOS_OK);
    ASSERT(read_counter() == 27);

    /* Ten more steps fit under EOS_ROLLBACK_MAX_STEP, but the bank holds
     * only five. Commit burns those five, then the sixth increment reports
     * the field full. */
    eos_rollback_stage(37);
    ASSERT(eos_rollback_commit() == EOS_ERR_FULL);

    /* The floor rose as far as the bank allowed, and no further. */
    ASSERT(read_counter() == STM32F4_OTP_COUNTER_MAX);
}

int main(void)
{
    printf("STM32F4 OTP-backed monotonic counter\n\n");

    run_test_counter_counts_burned_bits();
    run_test_next_burn_picks_the_lowest_set_bit();
    run_test_board_ops_wires_the_counter();
    run_test_otp_read_rejects_out_of_range();
    run_test_otp_write_programs_only_clearable_bits();
    run_test_monotonic_read_counts_the_field();
    run_test_monotonic_increment_burns_one_bit_at_a_time();
    run_test_increment_never_touches_anything_but_the_counter_field();
    run_test_increment_refuses_a_full_counter();
    run_test_commit_raises_the_floor_through_the_real_counter();
    run_test_commit_refuses_more_than_max_step();
    run_test_commit_fails_when_the_fuse_bank_runs_out();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
