// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_stm32f4_keystore_revoke.c
 * @brief Key revocation through the real STM32F4 OTP ops (#183).
 *
 * Revocation is clear-to-revoke: a burned (0) bit in the OTP revoke byte
 * revokes the slot, and the erased byte (0xFF) revokes nothing. That is
 * the only encoding OTP physics allows -- stm32f4_otp_write() refuses any
 * write that asks to set a bit -- so the old set-to-revoke encoding could
 * never persist: provisioning wrote the revoke byte to 0x00 (nothing
 * revoked, old reading), and revoking then asked the OTP to set a bit,
 * which came back EOS_ERR_INVALID while the revocation stayed RAM-only.
 * The same old reading made an erased revoke byte mean both slots
 * revoked, so a device whose revoke byte was never written had no active
 * key at all.
 *
 * board_stm32f4.c drives flash registers and is built only by a cross
 * build, so this suite compiles it on host with the OTP window and the
 * flash registers redirected at RAM buffers: EBLDR_TEST_OTP_WINDOW and
 * EBLDR_TEST_FLASH_REGS (see the board file). The tests below run the
 * real burn path -- eos_keystore_revoke_slot() persists through
 * stm32f4_otp_write(), whose set-bit refusal is the defect -- not a
 * model of it. The keys themselves are provisioned through the same
 * otp_write: key bytes over erased OTP only clear bits, so provisioning
 * accepts them, and the revoke byte is left erased, which is the
 * unrevoked state and needs no provisioning write.
 */

#include "board_stm32f4.h"
#include "eos_hal.h"
#include "eos_keystore.h"
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

/* Mirrors core/keystore.c: key slots at 0x100/0x120, revoke byte at 0x140. */
#define OTP_KEY0    0x100
#define OTP_KEY1    0x120
#define OTP_REVOKE  0x140

static void reset_fixture(void)
{
    memset(eboot_test_otp_window, 0xFF, sizeof(eboot_test_otp_window));
    memset(eboot_test_flash_regs, 0, sizeof(eboot_test_flash_regs));
    eos_hal_init(board_get_ops());
}

/* Provision both OTP key slots with distinguishable non-zero keys, through
 * the real otp_write. */
static void provision_keys(void)
{
    uint8_t key0[EOS_ED25519_PUB_KEY_SIZE];
    uint8_t key1[EOS_ED25519_PUB_KEY_SIZE];
    memset(key0, 0xA1, sizeof(key0));
    memset(key1, 0xB2, sizeof(key1));
    ASSERT(eos_hal_otp_write(OTP_KEY0, key0, sizeof(key0)) == EOS_OK);
    ASSERT(eos_hal_otp_write(OTP_KEY1, key1, sizeof(key1)) == EOS_OK);
}

static void assert_active_key_is(uint8_t fill)
{
    eos_keystore_t ks;
    const uint8_t *key = NULL;
    size_t key_len = 0;
    uint8_t expect[EOS_ED25519_PUB_KEY_SIZE];

    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) == EOS_OK);
    memset(expect, fill, sizeof(expect));
    ASSERT(key_len == sizeof(expect));
    ASSERT(memcmp(key, expect, sizeof(expect)) == 0);
}

/* An erased revoke byte revokes nothing: a freshly provisioned device has
 * an active key. Under the old set-to-revoke reading 0xFF meant both slots
 * revoked, so this device had none. */
TEST(test_erased_revoke_byte_revokes_nothing)
{
    reset_fixture();
    provision_keys();

    ASSERT(eboot_test_otp_window[OTP_REVOKE] == 0xFF);
    assert_active_key_is(0xA1);
}

/* Revoking slot 0 persists: the call returns EOS_OK, the revoke byte reads
 * back with bit 0 cleared, and a fresh init -- the next boot -- sees slot 0
 * revoked and slot 1 active. Under the old encoding the persist asked the
 * OTP to set a bit and came back EOS_ERR_INVALID. */
TEST(test_revoke_slot_zero_persists_through_real_otp_write)
{
    reset_fixture();
    provision_keys();

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_revoke_slot(&ks, 0) == EOS_OK);
    ASSERT(eboot_test_otp_window[OTP_REVOKE] == 0xFE);

    /* The next boot reads the persisted revocation, not the RAM copy. */
    assert_active_key_is(0xB2);
    uint32_t count = 99;
    ASSERT(eos_keystore_key_count(&ks, &count) == EOS_OK);
    ASSERT(count == 1);
}

/* Revoking the second slot leaves no active key, and the revoke byte
 * accumulates the burns instead of replacing them: 0xFE then 0xFC. */
TEST(test_revoke_both_slots_leaves_no_active_key)
{
    reset_fixture();
    provision_keys();

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_revoke_slot(&ks, 0) == EOS_OK);
    ASSERT(eboot_test_otp_window[OTP_REVOKE] == 0xFE);
    ASSERT(eos_keystore_revoke_slot(&ks, 1) == EOS_OK);
    ASSERT(eboot_test_otp_window[OTP_REVOKE] == 0xFC);

    eos_keystore_t ks2;
    ASSERT(eos_keystore_init(&ks2) == EOS_OK);
    const uint8_t *key = NULL;
    size_t key_len = 0;
    ASSERT(eos_keystore_get_active_key(&ks2, &key, &key_len) != EOS_OK);
    uint32_t count = 99;
    ASSERT(eos_keystore_key_count(&ks2, &count) == EOS_OK);
    ASSERT(count == 0);
}

/* Revoking an already-revoked slot is a no-op burn: it still succeeds and
 * the byte is unchanged, so a retry or a double revoke cannot disturb the
 * other slot's bit. */
TEST(test_revoke_is_idempotent)
{
    reset_fixture();
    provision_keys();

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_revoke_slot(&ks, 0) == EOS_OK);
    ASSERT(eboot_test_otp_window[OTP_REVOKE] == 0xFE);
    ASSERT(eos_keystore_revoke_slot(&ks, 0) == EOS_OK);
    ASSERT(eboot_test_otp_window[OTP_REVOKE] == 0xFE);

    assert_active_key_is(0xB2);
}

int main(void)
{
    printf("STM32F4 keystore revocation through the real OTP ops\n\n");

    run_test_erased_revoke_byte_revokes_nothing();
    run_test_revoke_slot_zero_persists_through_real_otp_write();
    run_test_revoke_both_slots_leaves_no_active_key();
    run_test_revoke_is_idempotent();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
