// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_watchdog_rollback.c
 * @brief A hung image is rolled back, not sent to recovery.
 *
 * A test-boot image that hangs is reset by the watchdog, and stage-1
 * increments boot_attempts before every jump. Once boot_attempts reaches
 * max_attempts, stage-1's eos_boot_policy_select() is meant to roll back to
 * the other slot. Stage-0's ebldr_recovery_triggered() checked the same
 * condition first -- watchdog reset with attempts exhausted -- and entered
 * recovery, so control never reached the rollback.
 *
 * This suite runs the real stage-0 trigger check (stage0/recovery_entry.c,
 * compiled for the host) and then the real stage-1 policy. Slot validity is
 * mocked at the slot-manager boundary: eos_slot_is_valid() /
 * eos_slot_scan_all() are defined here, so these tests do not need signed
 * images in flash.
 */

#include "eos_hal.h"
#include "eos_bootctl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool ebldr_recovery_triggered(const eos_bootctl_t *bctl);
extern int  eos_boot_policy_select(eos_bootctl_t *bctl, eos_slot_t *selected);

#define SIM_FLASH_SIZE   0x4000u
#define SIM_BOOTCTL      0x1000u
#define SIM_BOOTCTL_BAK  0x2000u
#define SIM_LOG          0x3000u

static uint8_t sim_flash[SIM_FLASH_SIZE];
static eos_reset_reason_t sim_reason;
static bool sim_pin;
static bool slot_valid[2];

/* ---- Slot manager boundary ---- */

bool eos_slot_is_valid(eos_slot_t slot)
{
    if (slot > EOS_SLOT_B) return false;
    return slot_valid[slot];
}

int eos_slot_scan_all(void)
{
    return (int)slot_valid[EOS_SLOT_A] + (int)slot_valid[EOS_SLOT_B];
}

/* ---- Simulated board ---- */

static int sim_range_ok(uint32_t addr, size_t len)
{
    return len <= SIM_FLASH_SIZE && (size_t)addr <= SIM_FLASH_SIZE - len;
}

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr], len);
    return EOS_OK;
}

static int sim_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memcpy(&sim_flash[addr], buf, len);
    return EOS_OK;
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memset(&sim_flash[addr], 0xFF, len);
    return EOS_OK;
}

static eos_reset_reason_t sim_reset_reason(void) { return sim_reason; }
static bool sim_recovery_pin(void) { return sim_pin; }

static const eos_board_ops_t sim_ops = {
    .flash_base            = 0,
    .flash_size            = SIM_FLASH_SIZE,
    .bootctl_addr          = SIM_BOOTCTL,
    .bootctl_backup_addr   = SIM_BOOTCTL_BAK,
    .log_addr              = SIM_LOG,
    .flash_read            = sim_flash_read,
    .flash_write           = sim_flash_write,
    .flash_erase           = sim_flash_erase,
    .get_reset_reason      = sim_reset_reason,
    .recovery_pin_asserted = sim_recovery_pin,
};

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        memset(sim_flash, 0xFF, sizeof(sim_flash)); \
        sim_reason = EOS_RESET_POWER_ON; \
        sim_pin = false; \
        slot_valid[EOS_SLOT_A] = true; \
        slot_valid[EOS_SLOT_B] = true; \
        eos_hal_init(&sim_ops); \
        printf("  %-58s ", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

/* Slot A confirmed good; Slot B under test boot and hanging: every boot of
 * it has been ended by the watchdog, max_attempts times. */
static void hung_test_boot_of_b(eos_bootctl_t *bctl)
{
    eos_bootctl_init_defaults(bctl);
    bctl->active_slot    = EOS_SLOT_B;
    bctl->confirmed_slot = EOS_SLOT_A;
    bctl->flags          = EOS_FLAG_TEST_BOOT;
    bctl->boot_attempts  = bctl->max_attempts;
    sim_reason = EOS_RESET_WATCHDOG;
}

TEST(test_hung_image_after_watchdog_rolls_back_to_good_slot)
{
    eos_bootctl_t bctl;
    eos_slot_t selected = EOS_SLOT_NONE;

    hung_test_boot_of_b(&bctl);

    /* Stage-0 must hand this boot to stage-1, not to recovery... */
    ASSERT(!ebldr_recovery_triggered(&bctl));

    /* ...where the policy rolls back to the confirmed slot. */
    ASSERT(eos_boot_policy_select(&bctl, &selected) == EOS_OK);
    ASSERT(selected == EOS_SLOT_A);
    ASSERT(bctl.active_slot == EOS_SLOT_A);
    ASSERT(bctl.boot_attempts == 0);
    ASSERT(bctl.flags & EOS_FLAG_ROLLBACK);
    ASSERT(!(bctl.flags & EOS_FLAG_TEST_BOOT));
}

TEST(test_hung_image_with_no_good_slot_still_fails_to_recovery)
{
    eos_bootctl_t bctl;
    eos_slot_t selected = EOS_SLOT_NONE;

    hung_test_boot_of_b(&bctl);
    slot_valid[EOS_SLOT_A] = false;

    /* Nothing to roll back to: the policy refuses, and stage-1 enters
     * recovery on that result. */
    ASSERT(!ebldr_recovery_triggered(&bctl));
    ASSERT(eos_boot_policy_select(&bctl, &selected) == EOS_ERR_NO_IMAGE);
    ASSERT(selected == EOS_SLOT_NONE);
}

TEST(test_explicit_recovery_triggers_are_unchanged)
{
    eos_bootctl_t bctl;

    eos_bootctl_init_defaults(&bctl);
    ASSERT(!ebldr_recovery_triggered(&bctl));

    sim_pin = true;
    ASSERT(ebldr_recovery_triggered(&bctl));
    sim_pin = false;

    bctl.flags = EOS_FLAG_FORCE_RECOVERY;
    ASSERT(ebldr_recovery_triggered(&bctl));

    bctl.flags = EOS_FLAG_FACTORY_RESET;
    ASSERT(ebldr_recovery_triggered(&bctl));
}

int main(void)
{
    printf("Watchdog reset of a hung image\n\n");

    run_test_hung_image_after_watchdog_rolls_back_to_good_slot();
    run_test_hung_image_with_no_good_slot_still_fails_to_recovery();
    run_test_explicit_recovery_triggers_are_unchanged();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
