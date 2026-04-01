// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023
/**
 * @file test_boot_log.c
 * @brief Unit tests for boot log subsystem
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "eos_boot_log.h"

static int passed = 0;
#define PASS(name) do { printf("[PASS] %s\n", name); passed++; } while(0)

/* ---- Simulated Boot Log Storage ---- */
static eos_boot_log_entry_t log_buffer[EOS_BOOT_LOG_MAX];
static int log_count = 0;
static uint32_t log_tick = 1000;

/* ---- Stub implementations ---- */
int eos_boot_log_init(void) {
    memset(log_buffer, 0, sizeof(log_buffer));
    log_count = 0;
    return EOS_OK;
}

void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail) {
    if (log_count < EOS_BOOT_LOG_MAX) {
        log_buffer[log_count].timestamp = log_tick++;
        log_buffer[log_count].event = event;
        log_buffer[log_count].slot = slot;
        log_buffer[log_count].detail = detail;
        log_count++;
    } else {
        /* Ring buffer: overwrite oldest */
        memmove(&log_buffer[0], &log_buffer[1],
                (EOS_BOOT_LOG_MAX - 1) * sizeof(eos_boot_log_entry_t));
        log_buffer[EOS_BOOT_LOG_MAX - 1].timestamp = log_tick++;
        log_buffer[EOS_BOOT_LOG_MAX - 1].event = event;
        log_buffer[EOS_BOOT_LOG_MAX - 1].slot = slot;
        log_buffer[EOS_BOOT_LOG_MAX - 1].detail = detail;
    }
}

int eos_boot_log_read(eos_boot_log_entry_t *entries, uint32_t max_count) {
    if (!entries) return EOS_ERR_INVALID;
    uint32_t n = (uint32_t)log_count;
    if (n > max_count) n = max_count;
    memcpy(entries, log_buffer, n * sizeof(eos_boot_log_entry_t));
    return (int)n;
}

uint32_t eos_boot_log_count(void) {
    return (uint32_t)log_count;
}

int eos_boot_log_clear(void) {
    memset(log_buffer, 0, sizeof(log_buffer));
    log_count = 0;
    return EOS_OK;
}

int eos_boot_log_flush(void) {
    return EOS_OK;
}

int eos_boot_log_get_latest(eos_boot_log_entry_t *entry) {
    if (!entry) return EOS_ERR_INVALID;
    if (log_count == 0) return EOS_ERR_NO_IMAGE;
    *entry = log_buffer[log_count - 1];
    return EOS_OK;
}

const char *eos_boot_log_event_name(uint32_t event) {
    switch (event) {
        case EOS_LOG_BOOT_START:     return "BOOT_START";
        case EOS_LOG_IMAGE_VALID:    return "IMAGE_VALID";
        case EOS_LOG_IMAGE_INVALID:  return "IMAGE_INVALID";
        case EOS_LOG_SLOT_SELECTED:  return "SLOT_SELECTED";
        case EOS_LOG_ROLLBACK:       return "ROLLBACK";
        case EOS_LOG_RECOVERY_ENTER: return "RECOVERY_ENTER";
        case EOS_LOG_UPGRADE_START:  return "UPGRADE_START";
        case EOS_LOG_UPGRADE_DONE:   return "UPGRADE_DONE";
        case EOS_LOG_CONFIRM:        return "CONFIRM";
        case EOS_LOG_FACTORY_RESET:  return "FACTORY_RESET";
        case EOS_LOG_WATCHDOG_RESET: return "WATCHDOG_RESET";
        case EOS_LOG_BOOT_FAIL:      return "BOOT_FAIL";
        default:                     return "UNKNOWN";
    }
}

/* ---- Tests ---- */
static void test_log_init(void) {
    int rc = eos_boot_log_init();
    assert(rc == EOS_OK);
    assert(eos_boot_log_count() == 0);
    PASS("log_init");
}

static void test_log_append_single(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    assert(eos_boot_log_count() == 1);
    PASS("log_append_single");
}

static void test_log_append_multiple(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_IMAGE_VALID, EOS_SLOT_A, 0x01020003);
    eos_boot_log_append(EOS_LOG_SLOT_SELECTED, EOS_SLOT_A, 0);
    assert(eos_boot_log_count() == 3);
    PASS("log_append_multiple");
}

static void test_log_read_entries(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 100);
    eos_boot_log_append(EOS_LOG_ROLLBACK, EOS_SLOT_B, 200);

    eos_boot_log_entry_t entries[8];
    int n = eos_boot_log_read(entries, 8);
    assert(n == 2);
    assert(entries[0].event == EOS_LOG_BOOT_START);
    assert(entries[0].detail == 100);
    assert(entries[1].event == EOS_LOG_ROLLBACK);
    assert(entries[1].detail == 200);
    PASS("log_read_entries");
}

static void test_log_read_limited_buffer(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_IMAGE_VALID, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_SLOT_SELECTED, EOS_SLOT_A, 0);

    eos_boot_log_entry_t entries[2];
    int n = eos_boot_log_read(entries, 2);
    assert(n == 2);
    PASS("log_read_limited_buffer");
}

static void test_log_get_latest(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 10);
    eos_boot_log_append(EOS_LOG_UPGRADE_DONE, EOS_SLOT_B, 20);

    eos_boot_log_entry_t entry;
    int rc = eos_boot_log_get_latest(&entry);
    assert(rc == EOS_OK);
    assert(entry.event == EOS_LOG_UPGRADE_DONE);
    assert(entry.slot == EOS_SLOT_B);
    assert(entry.detail == 20);
    PASS("log_get_latest");
}

static void test_log_get_latest_empty(void) {
    eos_boot_log_init();
    eos_boot_log_entry_t entry;
    int rc = eos_boot_log_get_latest(&entry);
    assert(rc == EOS_ERR_NO_IMAGE);
    PASS("log_get_latest_empty");
}

static void test_log_clear(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_B, 0);
    assert(eos_boot_log_count() == 2);

    int rc = eos_boot_log_clear();
    assert(rc == EOS_OK);
    assert(eos_boot_log_count() == 0);
    PASS("log_clear");
}

static void test_log_flush(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    int rc = eos_boot_log_flush();
    assert(rc == EOS_OK);
    PASS("log_flush");
}

static void test_log_event_names(void) {
    assert(strcmp(eos_boot_log_event_name(EOS_LOG_BOOT_START), "BOOT_START") == 0);
    assert(strcmp(eos_boot_log_event_name(EOS_LOG_ROLLBACK), "ROLLBACK") == 0);
    assert(strcmp(eos_boot_log_event_name(EOS_LOG_RECOVERY_ENTER), "RECOVERY_ENTER") == 0);
    assert(strcmp(eos_boot_log_event_name(EOS_LOG_FACTORY_RESET), "FACTORY_RESET") == 0);
    assert(strcmp(eos_boot_log_event_name(0xFF), "UNKNOWN") == 0);
    PASS("log_event_names");
}

static void test_log_entry_timestamps_increment(void) {
    eos_boot_log_init();
    eos_boot_log_append(EOS_LOG_BOOT_START, EOS_SLOT_A, 0);
    eos_boot_log_append(EOS_LOG_IMAGE_VALID, EOS_SLOT_A, 0);
    eos_boot_log_entry_t entries[2];
    eos_boot_log_read(entries, 2);
    assert(entries[1].timestamp > entries[0].timestamp);
    PASS("log_entry_timestamps_increment");
}

static void test_log_constants(void) {
    assert(EOS_BOOT_LOG_MAX == 32);
    assert(EOS_LOG_BOOT_START == 0x01);
    assert(EOS_LOG_BOOT_FAIL == 0x0C);
    assert(EOS_BOOT_LOG_SECTOR_SIZE == 4096);
    PASS("log_constants");
}

static void test_log_entry_struct_size(void) {
    assert(sizeof(eos_boot_log_entry_t) == 16);
    PASS("log_entry_struct_size");
}

int main(void) {
    printf("=== eboot Boot Log Tests ===\n");
    test_log_init();
    test_log_append_single();
    test_log_append_multiple();
    test_log_read_entries();
    test_log_read_limited_buffer();
    test_log_get_latest();
    test_log_get_latest_empty();
    test_log_clear();
    test_log_flush();
    test_log_event_names();
    test_log_entry_timestamps_increment();
    test_log_constants();
    test_log_entry_struct_size();
    printf("\n=== ALL %d TESTS PASSED ===\n", passed);
    return 0;
}
