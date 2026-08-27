// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_slot_manager.c
 * @brief Unit tests for the production firmware slot manager
 */

#include "eos_hal.h"
#include "eos_slot_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Simulated Flash Backend ---- */
#define SIM_FLASH_SIZE   (256 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];

#define SLOT_A_OFFSET   0x10000
#define SLOT_B_OFFSET   0x30000
#define SLOT_REC_OFFSET 0x20000

static eos_slot_state_t slot_states[3] = {
    EOS_SLOT_STATE_EMPTY, EOS_SLOT_STATE_EMPTY, EOS_SLOT_STATE_EMPTY
};
static uint32_t slot_versions[3] = {0, 0, 0};
static uint8_t slot_boot_attempts[3] = {0, 0, 0};
static bool slot_confirmed[3] = {false, false, false};

/* ---- Stub implementations ---- */
int eos_slot_scan_all(void) {
    int valid = 0;
    for (int i = 0; i < 3; i++) {
        if (slot_states[i] == EOS_SLOT_STATE_VALID ||
            slot_states[i] == EOS_SLOT_STATE_CONFIRMED)
            valid++;
    }
    erase_result = EOS_OK;
    erased_addr = 0;
    erased_size = 0;
}

uint32_t eos_hal_slot_addr(eos_slot_t slot)
{
    if (slot == EOS_SLOT_A) return SLOT_A_ADDR;
    if (slot == EOS_SLOT_B) return SLOT_B_ADDR;
    return 0;
}

uint32_t eos_hal_slot_size(eos_slot_t slot)
{
    return slot <= EOS_SLOT_B ? SLOT_SIZE : 0;
}

int eos_hal_flash_erase(uint32_t addr, size_t len)
{
    erased_addr = addr;
    erased_size = len;
    return erase_result;
}

int eos_image_parse_header(uint32_t addr, eos_image_header_t *out)
{
    int slot = slot_index(addr);
    if (slot < 0 || !out) return EOS_ERR_INVALID;
    if (parse_result[slot] != EOS_OK) return parse_result[slot];

int eos_slot_erase(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return EOS_ERR_INVALID;
    slot_states[slot] = EOS_SLOT_STATE_EMPTY;
    slot_versions[slot] = 0;
    slot_boot_attempts[slot] = 0;
    slot_confirmed[slot] = false;
    return EOS_OK;
}

int eos_slot_mark_booting(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return EOS_ERR_INVALID;
    if (slot_boot_attempts[slot] < 255) {
        slot_boot_attempts[slot]++;
    }
    return EOS_OK;
}

int eos_slot_confirm(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return EOS_ERR_INVALID;
    slot_boot_attempts[slot] = 0;
    slot_confirmed[slot] = true;
    if (slot_states[slot] == EOS_SLOT_STATE_VALID) {
        slot_states[slot] = EOS_SLOT_STATE_CONFIRMED;
    }
    return EOS_OK;
}

bool eos_slot_needs_rollback(eos_slot_t slot, uint8_t max_attempts) {
    if (slot > EOS_SLOT_RECOVERY || max_attempts == 0) return false;
    return slot_boot_attempts[slot] >= max_attempts;
}

uint8_t eos_slot_get_boot_attempts(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return 0;
    return slot_boot_attempts[slot];
}

/* ---- Helper ---- */
static void reset_slots(void) {
    memset(sim_flash, 0xFF, SIM_FLASH_SIZE);
    for (int i = 0; i < 3; i++) {
        slot_states[i] = EOS_SLOT_STATE_EMPTY;
        slot_versions[i] = 0;
        slot_boot_attempts[i] = 0;
        slot_confirmed[i] = false;
    }
}

/* ---- Tests ---- */
static void test_scan_no_valid_slots(void) {
    reset_slots();
    int count = eos_slot_scan_all();
    assert(count == 0);
    PASS("scan_no_valid_slots");
}

int eos_image_verify_signature(const eos_image_header_t *hdr)
{
    if (!hdr || hdr->reserved[0] > EOS_SLOT_B) return EOS_ERR_INVALID;
    return signature_result[hdr->reserved[0]];
}

static void make_valid(eos_slot_t slot, uint32_t version)
{
    parse_result[slot] = EOS_OK;
    slot_version[slot] = version;
}

static void test_scan_no_valid_slots(void)
{
    ASSERT(eos_slot_scan_all() == 0);
    ASSERT(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_EMPTY);
    ASSERT(eos_slot_get_state(EOS_SLOT_B) == EOS_SLOT_STATE_EMPTY);
}

static void test_scan_one_valid_slot(void)
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

static void test_scan_two_valid_slots(void)
{
    make_valid(EOS_SLOT_A, EOS_VERSION_MAKE(1, 0, 0));
    make_valid(EOS_SLOT_B, EOS_VERSION_MAKE(2, 0, 0));

    ASSERT(eos_slot_scan_all() == 2);
    ASSERT(eos_slot_is_valid(EOS_SLOT_A));
    ASSERT(eos_slot_is_valid(EOS_SLOT_B));
    ASSERT(eos_slot_get_version(EOS_SLOT_B) == EOS_VERSION_MAKE(2, 0, 0));
}

static void test_verification_failures_are_invalid(void)
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

static void test_invalid_slot_is_rejected(void)
{
    ASSERT(!eos_slot_is_valid(EOS_SLOT_RECOVERY));
    ASSERT(eos_slot_get_state(EOS_SLOT_RECOVERY) == EOS_SLOT_STATE_EMPTY);
    ASSERT(eos_slot_get_version(EOS_SLOT_RECOVERY) == 0);
    ASSERT(eos_slot_get_header(EOS_SLOT_RECOVERY, NULL) == EOS_ERR_INVALID);
    ASSERT(eos_slot_erase(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);
}

static void test_erase_updates_state_only_on_success(void)
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

static void test_boot_attempts_and_rollback(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    assert(eos_slot_get_boot_attempts(EOS_SLOT_A) == 0);
    assert(!eos_slot_needs_rollback(EOS_SLOT_A, 3));

    // Attempt 1
    assert(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
    assert(eos_slot_get_boot_attempts(EOS_SLOT_A) == 1);
    assert(!eos_slot_needs_rollback(EOS_SLOT_A, 3));

    // Attempt 2
    assert(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
    assert(eos_slot_get_boot_attempts(EOS_SLOT_A) == 2);
    assert(!eos_slot_needs_rollback(EOS_SLOT_A, 3));

    // Attempt 3 (hits max allowed 3)
    assert(eos_slot_mark_booting(EOS_SLOT_A) == EOS_OK);
    assert(eos_slot_get_boot_attempts(EOS_SLOT_A) == 3);
    assert(eos_slot_needs_rollback(EOS_SLOT_A, 3));

    // Confirm slot (resets boot attempts and confirms)
    assert(eos_slot_confirm(EOS_SLOT_A) == EOS_OK);
    assert(eos_slot_get_boot_attempts(EOS_SLOT_A) == 0);
    assert(!eos_slot_needs_rollback(EOS_SLOT_A, 3));
    assert(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_CONFIRMED);

    // Invalid slot handles
    assert(eos_slot_mark_booting((eos_slot_t)0xFE) == EOS_ERR_INVALID);
    assert(eos_slot_confirm((eos_slot_t)0xFE) == EOS_ERR_INVALID);
    assert(eos_slot_get_boot_attempts((eos_slot_t)0xFE) == 0);
    assert(!eos_slot_needs_rollback((eos_slot_t)0xFE, 3));
    PASS("boot_attempts_and_rollback");
}

int main(void) {
    printf("=== eboot Slot Manager Tests ===\n");
    test_scan_no_valid_slots();
    test_scan_one_valid_slot();
    test_scan_two_valid_slots();
    test_scan_all_slots_valid();
    test_slot_is_valid_empty();
    test_slot_is_valid_with_image();
    test_slot_is_valid_confirmed();
    test_slot_is_valid_invalid_state();
    test_slot_get_version_empty();
    test_slot_get_version_with_image();
    test_slot_get_state_empty();
    test_slot_get_state_valid();
    test_slot_get_state_testing();
    test_slot_erase();
    test_slot_erase_already_empty();
    test_slot_get_header_valid();
    test_slot_get_header_empty();
    test_slot_get_header_null();
    test_version_macro_encoding();
    test_version_macro_max_values();
    test_slot_enum_values();
    test_boot_attempts_and_rollback();
    printf("\n=== ALL %d TESTS PASSED ===\n", passed);
    return 0;
}
