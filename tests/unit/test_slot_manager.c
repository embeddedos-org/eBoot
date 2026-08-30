// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_slot_manager.c
 * @brief Unit tests for the production firmware slot manager
 *
 * These tests exercise the real core/slot_manager.c. The three image
 * verification steps it calls -- eos_image_parse_header(),
 * eos_image_verify_integrity() and eos_image_verify_signature() -- are
 * replaced with per-slot scriptable mocks so each stage can be failed
 * independently without having to build and sign real images. Flash and
 * slot geometry come from the standard simulated eos_board_ops_t, so
 * eos_hal_slot_addr()/eos_hal_slot_size() behave as they do on a board.
 */

#include "eos_slot_manager.h"
#include "eos_image.h"
#include "eos_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_A_ADDR 0x10000u
#define SLOT_B_ADDR 0x30000u
#define SLOT_SIZE   0x10000u

static int parse_result[2];
static int integrity_result[2];
static int signature_result[2];
static uint32_t slot_version[2];
static int erase_result;
static uint32_t erased_addr;
static size_t erased_size;
static int tests_passed;

#define ASSERT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "[FAIL] %s:%d: %s\n",                          \
                    __FILE__, __LINE__, #condition);                          \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define RUN(test)                                                             \
    do {                                                                      \
        reset_fixture();                                                      \
        test();                                                               \
        tests_passed++;                                                       \
        printf("[PASS] %s\n", #test);                                       \
    } while (0)

static int slot_index(uint32_t addr)
{
    if (addr == SLOT_A_ADDR) return EOS_SLOT_A;
    if (addr == SLOT_B_ADDR) return EOS_SLOT_B;
    return -1;
}

static void reset_fixture(void)
{
    for (int i = 0; i < 2; i++) {
        parse_result[i] = EOS_ERR_NO_IMAGE;
        integrity_result[i] = EOS_OK;
        signature_result[i] = EOS_OK;
        slot_version[i] = 0;
    }
    erase_result = EOS_OK;
    erased_addr = 0;
    erased_size = 0;
}

static int      parse_result[2];
static int      integrity_result[2];
static int      signature_result[2];
static uint32_t slot_version[2];

/* ---- Observed flash erase behaviour ---- */

static int      erase_result;
static uint32_t erased_addr;
static size_t   erased_size;

/** Map a flash address back to the slot that starts there, or -1. */
static int slot_index(uint32_t addr)
{
    if (addr == SLOT_A_ADDR) return EOS_SLOT_A;
    if (addr == SLOT_B_ADDR) return EOS_SLOT_B;
    return -1;
}

/* ---- Image verification mocks (override eboot_core's real ones) ----
 *
 * verify_slot() passes the parsed header straight to the integrity and
 * signature checks, so the mocks stash the slot index in reserved[0] on
 * parse and read it back to decide which scripted result to return. */

int eos_image_parse_header(uint32_t addr, eos_image_header_t *out)
{
    int slot = slot_index(addr);
    if (slot < 0 || !out) return EOS_ERR_INVALID;
    if (parse_result[slot] != EOS_OK) return parse_result[slot];

    memset(out, 0, sizeof(*out));
    out->magic = EOS_IMG_MAGIC;
    out->image_version = slot_version[slot];
    out->reserved[0] = (uint8_t)slot;
    return EOS_OK;
}

int eos_image_verify_integrity(const eos_image_header_t *hdr, uint32_t addr)
{
    int slot = slot_index(addr);
    if (!hdr || slot < 0) return EOS_ERR_INVALID;
    return integrity_result[slot];
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
    erased_addr = addr;
    erased_size = len;
    if (erase_result != EOS_OK) return erase_result;
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memset(&sim_flash[addr], 0xFF, len);
    return EOS_OK;
}

static uint32_t sim_tick = 0;
static uint32_t sim_get_tick(void) { return sim_tick++; }
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t x) { (void)x; }
static void sim_jump(uint32_t addr) { (void)addr; }
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static bool sim_recovery_pin(void) { return false; }
static void sim_system_reset(void) {}

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size          = SIM_FLASH_SIZE,
    .slot_a_addr         = SLOT_A_ADDR,
    .slot_a_size         = SLOT_SIZE,
    .slot_b_addr         = SLOT_B_ADDR,
    .slot_b_size         = SLOT_SIZE,
    .recovery_addr       = 0,
    .recovery_size       = 0,
    .bootctl_addr        = 0,
    .bootctl_backup_addr = 0x1000,
    .log_addr            = 0x2000,
    .app_vector_offset   = 0,
    .flash_read          = sim_flash_read,
    .flash_write         = sim_flash_write,
    .flash_erase         = sim_flash_erase,
    .watchdog_init       = sim_noop_u32,
    .watchdog_feed       = sim_noop,
    .get_reset_reason    = sim_reset_reason,
    .system_reset        = sim_system_reset,
    .recovery_pin_asserted = sim_recovery_pin,
    .jump                = sim_jump,
    .uart_init           = NULL,
    .uart_send           = NULL,
    .uart_recv           = NULL,
    .get_tick_ms         = sim_get_tick,
    .disable_interrupts  = sim_noop,
    .enable_interrupts   = sim_noop,
    .deinit_peripherals  = sim_noop,
};

/* ---- Test harness ---- */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        memset(sim_flash, 0xFF, sizeof(sim_flash)); \
        sim_tick = 0; \
        for (int i = 0; i < 2; i++) { \
            parse_result[i] = EOS_ERR_NO_IMAGE; \
            integrity_result[i] = EOS_OK; \
            signature_result[i] = EOS_OK; \
            slot_version[i] = 0; \
        } \
        erase_result = EOS_OK; \
        erased_addr = 0; \
        erased_size = 0; \
        eos_hal_init(&sim_ops); \
        printf("  %-55s ", #name); \
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

/** Script a slot so that all three verification stages succeed. */
static void make_valid(eos_slot_t slot, uint32_t version)
{
    parse_result[slot] = EOS_OK;
    slot_version[slot] = version;
}

/* ---- Tests ---- */

TEST(test_scan_no_valid_slots)
{
    ASSERT(eos_slot_scan_all() == 0);
    ASSERT(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_EMPTY);
    ASSERT(eos_slot_get_state(EOS_SLOT_B) == EOS_SLOT_STATE_EMPTY);
}

TEST(test_scan_one_valid_slot)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 2, 3));

    ASSERT(eos_slot_scan_all() == 1);
    ASSERT(eos_slot_is_valid(EOS_SLOT_A));
    ASSERT(!eos_slot_is_valid(EOS_SLOT_B));
    ASSERT(eos_slot_get_version(EOS_SLOT_A) == EOS_VERSION_MAKE(1, 2, 3));

    eos_image_header_t header;
    ASSERT(eos_slot_get_header(EOS_SLOT_A, &header) == EOS_OK);
    ASSERT(header.image_version == EOS_VERSION_MAKE(1, 2, 3));
}

TEST(test_scan_two_valid_slots)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    make_valid(EOS_SLOT_B, EOS_VERSION_MAKE(2, 0, 0));

    ASSERT(eos_slot_scan_all() == 2);
    ASSERT(eos_slot_is_valid(EOS_SLOT_A));
    ASSERT(eos_slot_is_valid(EOS_SLOT_B));
    ASSERT(eos_slot_get_version(EOS_SLOT_B) == EOS_VERSION_MAKE(2, 0, 0));
}

TEST(test_verification_failures_are_invalid)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    make_valid(EOS_SLOT_B, EOS_VERSION_MAKE(2, 0, 0));
    integrity_result[EOS_SLOT_A] = EOS_ERR_CRC;
    signature_result[EOS_SLOT_B] = EOS_ERR_SIGNATURE;

    ASSERT(eos_slot_scan_all() == 0);
    ASSERT(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_INVALID);
    ASSERT(eos_slot_get_state(EOS_SLOT_B) == EOS_SLOT_STATE_INVALID);
    ASSERT(eos_slot_get_header(EOS_SLOT_A, NULL) == EOS_ERR_INVALID);
}

TEST(test_invalid_slot_is_rejected)
{
    ASSERT(!eos_slot_is_valid(EOS_SLOT_RECOVERY));
    ASSERT(eos_slot_get_state(EOS_SLOT_RECOVERY) == EOS_SLOT_STATE_EMPTY);
    ASSERT(eos_slot_get_version(EOS_SLOT_RECOVERY) == 0);
    ASSERT(eos_slot_get_header(EOS_SLOT_RECOVERY, NULL) == EOS_ERR_INVALID);
    ASSERT(eos_slot_erase(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);
}

TEST(test_erase_updates_state_only_on_success)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    ASSERT(eos_slot_scan_all() == 1);

    erase_result = EOS_ERR_FLASH;
    ASSERT(eos_slot_erase(EOS_SLOT_A) == EOS_ERR_FLASH);
    ASSERT(eos_slot_is_valid(EOS_SLOT_A));

    erase_result = EOS_OK;
    ASSERT(eos_slot_erase(EOS_SLOT_A) == EOS_OK);
    ASSERT(erased_addr == SLOT_A_ADDR);
    ASSERT(erased_size == SLOT_SIZE);
    ASSERT(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_EMPTY);
    ASSERT(eos_slot_get_version(EOS_SLOT_A) == 0);
}


/* The boot-attempt counter is what makes an unproven image fall back instead
 * of bricking the device: mark_booting() has to increment it, confirm() has to
 * clear it, and needs_rollback() has to fire once it reaches max_attempts.
 * PR #37 added this behaviour but its test never compiled, so none of it was
 * ever exercised. */
static void test_boot_attempts_drive_rollback(void)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    ASSERT(eos_slot_scan_all() == 1);

    ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_A) == 0);
    ASSERT(!eos_slot_needs_rollback(EOS_SLOT_A, 3));

    for (uint8_t attempt = 1; attempt <= 2; attempt++) {
        ASSERT(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
        ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_A) == attempt);
        ASSERT(!eos_slot_needs_rollback(EOS_SLOT_A, 3));
    }

    /* The third attempt reaches the limit. */
    ASSERT(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
    ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_A) == 3);
    ASSERT(eos_slot_needs_rollback(EOS_SLOT_A, 3));

    /* Confirming clears the counter and promotes the slot. */
    ASSERT(eos_slot_confirm(EOS_SLOT_A) == EOS_OK);
    ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_A) == 0);
    ASSERT(!eos_slot_needs_rollback(EOS_SLOT_A, 3));
    ASSERT(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_CONFIRMED);
}

static void test_boot_attempts_reject_invalid_slot(void)
{
    ASSERT(eos_slot_mark_booting(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);
    ASSERT(eos_slot_confirm(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);
    ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_RECOVERY) == 0);
    ASSERT(!eos_slot_needs_rollback(EOS_SLOT_RECOVERY, 3));

    /* max_attempts == 0 must never demand a rollback. */
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    ASSERT(eos_slot_scan_all() == 1);
    ASSERT(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
    ASSERT(!eos_slot_needs_rollback(EOS_SLOT_A, 0));
}

/* Erasing a slot must also drop its boot-attempt count; otherwise a freshly
 * flashed image inherits the failures of the one it replaced. */
static void test_erase_resets_boot_attempts(void)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    ASSERT(eos_slot_scan_all() == 1);

    /* eos_slot_scan_all() deliberately preserves the counter across a rescan,
     * so start from whatever it is and check the delta. */
    uint8_t before = eos_slot_get_boot_attempts(EOS_SLOT_A);
    ASSERT(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
    ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_A) == before + 1);

    ASSERT(eos_slot_erase(EOS_SLOT_A) == EOS_OK);
    ASSERT(eos_slot_get_boot_attempts(EOS_SLOT_A) == 0);
}

int main(void)
{
    printf("=== eBootloader Slot Manager Tests ===\n");
    RUN(test_scan_no_valid_slots);
    RUN(test_scan_one_valid_slot);
    RUN(test_scan_two_valid_slots);
    RUN(test_verification_failures_are_invalid);
    RUN(test_invalid_slot_is_rejected);
    RUN(test_erase_updates_state_only_on_success);
    RUN(test_boot_attempts_drive_rollback);
    RUN(test_boot_attempts_reject_invalid_slot);
    RUN(test_erase_resets_boot_attempts);
    printf("\n%d/9 tests passed\n", tests_passed);
    return 0;
}
