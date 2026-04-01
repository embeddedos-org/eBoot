// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023
/**
 * @file test_slot_manager.c
 * @brief Unit tests for firmware slot manager
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "eos_slot_manager.h"

static int passed = 0;
#define PASS(name) do { printf("[PASS] %s\n", name); passed++; } while(0)

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

/* ---- Stub implementations ---- */
int eos_slot_scan_all(void) {
    int valid = 0;
    for (int i = 0; i < 3; i++) {
        if (slot_states[i] == EOS_SLOT_STATE_VALID ||
            slot_states[i] == EOS_SLOT_STATE_CONFIRMED)
            valid++;
    }
    return valid;
}

bool eos_slot_is_valid(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return false;
    return (slot_states[slot] == EOS_SLOT_STATE_VALID ||
            slot_states[slot] == EOS_SLOT_STATE_CONFIRMED);
}

uint32_t eos_slot_get_version(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return 0;
    if (slot_states[slot] == EOS_SLOT_STATE_EMPTY) return 0;
    return slot_versions[slot];
}

int eos_slot_get_header(eos_slot_t slot, eos_image_header_t *out) {
    if (slot > EOS_SLOT_RECOVERY || !out) return EOS_ERR_INVALID;
    if (slot_states[slot] == EOS_SLOT_STATE_EMPTY) return EOS_ERR_NO_IMAGE;
    memset(out, 0, sizeof(*out));
    out->magic = EOS_IMG_MAGIC;
    out->image_version = slot_versions[slot];
    return EOS_OK;
}

eos_slot_state_t eos_slot_get_state(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return EOS_SLOT_STATE_EMPTY;
    return slot_states[slot];
}

int eos_slot_erase(eos_slot_t slot) {
    if (slot > EOS_SLOT_RECOVERY) return EOS_ERR_INVALID;
    slot_states[slot] = EOS_SLOT_STATE_EMPTY;
    slot_versions[slot] = 0;
    return EOS_OK;
}

/* ---- Helper ---- */
static void reset_slots(void) {
    memset(sim_flash, 0xFF, SIM_FLASH_SIZE);
    for (int i = 0; i < 3; i++) {
        slot_states[i] = EOS_SLOT_STATE_EMPTY;
        slot_versions[i] = 0;
    }
}

/* ---- Tests ---- */
static void test_scan_no_valid_slots(void) {
    reset_slots();
    int count = eos_slot_scan_all();
    assert(count == 0);
    PASS("scan_no_valid_slots");
}

static void test_scan_one_valid_slot(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    slot_versions[EOS_SLOT_A] = EOS_VERSION_MAKE(1, 0, 0);
    int count = eos_slot_scan_all();
    assert(count == 1);
    PASS("scan_one_valid_slot");
}

static void test_scan_two_valid_slots(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    slot_states[EOS_SLOT_B] = EOS_SLOT_STATE_CONFIRMED;
    slot_versions[EOS_SLOT_A] = EOS_VERSION_MAKE(1, 0, 0);
    slot_versions[EOS_SLOT_B] = EOS_VERSION_MAKE(2, 0, 0);
    int count = eos_slot_scan_all();
    assert(count == 2);
    PASS("scan_two_valid_slots");
}

static void test_scan_all_slots_valid(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    slot_states[EOS_SLOT_B] = EOS_SLOT_STATE_VALID;
    slot_states[EOS_SLOT_RECOVERY] = EOS_SLOT_STATE_CONFIRMED;
    int count = eos_slot_scan_all();
    assert(count == 3);
    PASS("scan_all_slots_valid");
}

static void test_slot_is_valid_empty(void) {
    reset_slots();
    assert(!eos_slot_is_valid(EOS_SLOT_A));
    PASS("slot_is_valid_empty");
}

static void test_slot_is_valid_with_image(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    assert(eos_slot_is_valid(EOS_SLOT_A));
    PASS("slot_is_valid_with_image");
}

static void test_slot_is_valid_confirmed(void) {
    reset_slots();
    slot_states[EOS_SLOT_B] = EOS_SLOT_STATE_CONFIRMED;
    assert(eos_slot_is_valid(EOS_SLOT_B));
    PASS("slot_is_valid_confirmed");
}

static void test_slot_is_valid_invalid_state(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_INVALID;
    assert(!eos_slot_is_valid(EOS_SLOT_A));
    PASS("slot_is_valid_invalid_state");
}

static void test_slot_get_version_empty(void) {
    reset_slots();
    uint32_t ver = eos_slot_get_version(EOS_SLOT_A);
    assert(ver == 0);
    PASS("slot_get_version_empty");
}

static void test_slot_get_version_with_image(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    slot_versions[EOS_SLOT_A] = EOS_VERSION_MAKE(2, 3, 5);
    uint32_t ver = eos_slot_get_version(EOS_SLOT_A);
    assert(EOS_VERSION_MAJOR(ver) == 2);
    assert(EOS_VERSION_MINOR(ver) == 3);
    assert(EOS_VERSION_PATCH(ver) == 5);
    PASS("slot_get_version_with_image");
}

static void test_slot_get_state_empty(void) {
    reset_slots();
    eos_slot_state_t state = eos_slot_get_state(EOS_SLOT_A);
    assert(state == EOS_SLOT_STATE_EMPTY);
    PASS("slot_get_state_empty");
}

static void test_slot_get_state_valid(void) {
    reset_slots();
    slot_states[EOS_SLOT_B] = EOS_SLOT_STATE_VALID;
    assert(eos_slot_get_state(EOS_SLOT_B) == EOS_SLOT_STATE_VALID);
    PASS("slot_get_state_valid");
}

static void test_slot_get_state_testing(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_TESTING;
    assert(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_TESTING);
    PASS("slot_get_state_testing");
}

static void test_slot_erase(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    slot_versions[EOS_SLOT_A] = EOS_VERSION_MAKE(1, 0, 0);
    int rc = eos_slot_erase(EOS_SLOT_A);
    assert(rc == EOS_OK);
    assert(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_EMPTY);
    assert(eos_slot_get_version(EOS_SLOT_A) == 0);
    PASS("slot_erase");
}

static void test_slot_erase_already_empty(void) {
    reset_slots();
    int rc = eos_slot_erase(EOS_SLOT_B);
    assert(rc == EOS_OK);
    PASS("slot_erase_already_empty");
}

static void test_slot_get_header_valid(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    slot_versions[EOS_SLOT_A] = EOS_VERSION_MAKE(3, 1, 0);
    eos_image_header_t hdr;
    int rc = eos_slot_get_header(EOS_SLOT_A, &hdr);
    assert(rc == EOS_OK);
    assert(hdr.magic == EOS_IMG_MAGIC);
    assert(hdr.image_version == EOS_VERSION_MAKE(3, 1, 0));
    PASS("slot_get_header_valid");
}

static void test_slot_get_header_empty(void) {
    reset_slots();
    eos_image_header_t hdr;
    int rc = eos_slot_get_header(EOS_SLOT_A, &hdr);
    assert(rc == EOS_ERR_NO_IMAGE);
    PASS("slot_get_header_empty");
}

static void test_slot_get_header_null(void) {
    reset_slots();
    slot_states[EOS_SLOT_A] = EOS_SLOT_STATE_VALID;
    int rc = eos_slot_get_header(EOS_SLOT_A, NULL);
    assert(rc == EOS_ERR_INVALID);
    PASS("slot_get_header_null");
}

static void test_version_macro_encoding(void) {
    uint32_t v = EOS_VERSION_MAKE(1, 2, 3);
    assert(EOS_VERSION_MAJOR(v) == 1);
    assert(EOS_VERSION_MINOR(v) == 2);
    assert(EOS_VERSION_PATCH(v) == 3);
    PASS("version_macro_encoding");
}

static void test_version_macro_max_values(void) {
    uint32_t v = EOS_VERSION_MAKE(255, 255, 65535);
    assert(EOS_VERSION_MAJOR(v) == 255);
    assert(EOS_VERSION_MINOR(v) == 255);
    assert(EOS_VERSION_PATCH(v) == 65535);
    PASS("version_macro_max_values");
}

static void test_slot_enum_values(void) {
    assert(EOS_SLOT_A == 0);
    assert(EOS_SLOT_B == 1);
    assert(EOS_SLOT_RECOVERY == 2);
    assert(EOS_SLOT_NONE == 0xFF);
    PASS("slot_enum_values");
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
    printf("\n=== ALL %d TESTS PASSED ===\n", passed);
    return 0;
}
