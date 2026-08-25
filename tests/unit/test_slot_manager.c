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

int main(void)
{
    printf("=== eBootloader Slot Manager Tests ===\n");
    RUN(test_scan_no_valid_slots);
    RUN(test_scan_one_valid_slot);
    RUN(test_scan_two_valid_slots);
    RUN(test_verification_failures_are_invalid);
    RUN(test_invalid_slot_is_rejected);
    RUN(test_erase_updates_state_only_on_success);
    printf("\n%d/6 tests passed\n", tests_passed);
    return 0;
}
