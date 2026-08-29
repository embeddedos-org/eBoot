// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC 15288:2023

/**
 * @file test_image_tlv.c
 * @brief Host tests for the firmware image TLV parser
 *
 * core/image_tlv.c is compiled into eboot_core but had no unit coverage.
 * These tests pin parse, find, and read_data against a simulated flash,
 * including truncated entries and the documented magic-mismatch code.
 */

#include "eos_image_tlv.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SIM_FLASH_SIZE  (64 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr], len);
    return EOS_OK;
}

static int sim_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memcpy(&sim_flash[addr], buf, len);
    return EOS_OK;
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
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
    .slot_a_addr         = 0x4000,
    .slot_a_size         = 0x8000,
    .slot_b_addr         = 0xC000,
    .slot_b_size         = 0x8000,
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

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        memset(sim_flash, 0xFF, sizeof(sim_flash)); \
        sim_tick = 0; \
        eos_hal_init(&sim_ops); \
        printf("  %-50s ", #name); \
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

static uint32_t tlv_align(uint32_t n)
{
    return (n + 3u) & ~3u;
}

/* Pack one TLV entry at dst; returns bytes consumed including 4-byte pad. */
static size_t pack_entry(uint8_t *dst, uint16_t type, const void *data, uint16_t len)
{
    eos_tlv_entry_hdr_t hdr;
    hdr.type = type;
    hdr.len = len;
    memcpy(dst, &hdr, sizeof(hdr));
    if (len > 0 && data != NULL) {
        memcpy(dst + sizeof(hdr), data, len);
    }
    size_t n = sizeof(hdr) + len;
    size_t aligned = tlv_align((uint32_t)n);
    if (aligned > n) {
        memset(dst + n, 0, aligned - n);
    }
    return aligned;
}

static void write_tlv_area(uint32_t addr, const uint8_t *entries, uint16_t entries_len)
{
    eos_tlv_info_t info;
    info.magic = EOS_TLV_INFO_MAGIC;
    info.tlv_total_len = (uint16_t)(sizeof(info) + entries_len);
    memcpy(&sim_flash[addr], &info, sizeof(info));
    if (entries_len > 0) {
        memcpy(&sim_flash[addr + sizeof(info)], entries, entries_len);
    }
}

TEST(test_parse_null_ctx)
{
    ASSERT(eos_tlv_parse(NULL, 0x1000) == EOS_ERR_INVALID);
}

TEST(test_parse_bad_magic_is_not_found)
{
    eos_tlv_info_t info;
    eos_tlv_ctx_t ctx;

    info.magic = 0x0000;
    info.tlv_total_len = sizeof(info);
    memcpy(&sim_flash[0x1000], &info, sizeof(info));

    /* No TLV area is a missing optional region, not a malformed header. */
    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_ERR_NOT_FOUND);
}

TEST(test_parse_oversized_tlv_is_invalid)
{
    eos_tlv_info_t info;
    eos_tlv_ctx_t ctx;

    info.magic = EOS_TLV_INFO_MAGIC;
    info.tlv_total_len = EOS_TLV_MAX_SIZE + 1;
    memcpy(&sim_flash[0x1000], &info, sizeof(info));

    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_ERR_INVALID);
}

TEST(test_parse_undersized_tlv_is_invalid)
{
    eos_tlv_info_t info;
    eos_tlv_ctx_t ctx;

    info.magic = EOS_TLV_INFO_MAGIC;
    info.tlv_total_len = 2; /* smaller than the info header itself */
    memcpy(&sim_flash[0x1000], &info, sizeof(info));

    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_ERR_INVALID);
}

TEST(test_parse_and_find_two_entries)
{
    uint8_t sha[32];
    uint8_t keyhash[32];
    uint8_t packed[128];
    size_t n = 0;
    eos_tlv_ctx_t ctx;
    eos_tlv_parsed_entry_t entry;
    uint8_t out[32];

    memset(sha, 0xA5, sizeof(sha));
    memset(keyhash, 0x5A, sizeof(keyhash));

    n += pack_entry(packed + n, EOS_TLV_SHA256, sha, sizeof(sha));
    n += pack_entry(packed + n, EOS_TLV_KEYHASH, keyhash, sizeof(keyhash));
    write_tlv_area(0x1000, packed, (uint16_t)n);

    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_OK);
    ASSERT(ctx.count == 2);
    ASSERT(ctx.total_len == (uint16_t)(sizeof(eos_tlv_info_t) + n));

    ASSERT(eos_tlv_find(&ctx, EOS_TLV_SHA256, &entry) == EOS_OK);
    ASSERT(entry.type == EOS_TLV_SHA256);
    ASSERT(entry.len == sizeof(sha));
    ASSERT(eos_tlv_read_data(&ctx, &entry, out, sizeof(out)) == EOS_OK);
    ASSERT(memcmp(out, sha, sizeof(sha)) == 0);

    ASSERT(eos_tlv_find(&ctx, EOS_TLV_KEYHASH, &entry) == EOS_OK);
    ASSERT(entry.type == EOS_TLV_KEYHASH);
    ASSERT(eos_tlv_read_data(&ctx, &entry, out, sizeof(out)) == EOS_OK);
    ASSERT(memcmp(out, keyhash, sizeof(keyhash)) == 0);

    ASSERT(eos_tlv_find(&ctx, EOS_TLV_NONCE, &entry) == EOS_ERR_NOT_FOUND);
}

TEST(test_odd_length_entry_alignment)
{
    /* A 1-byte payload is padded to 4 so the next header is aligned. */
    uint8_t nonce = 0x42;
    uint8_t sha[32];
    uint8_t packed[64];
    size_t n = 0;
    eos_tlv_ctx_t ctx;
    eos_tlv_parsed_entry_t entry;
    uint8_t out_nonce = 0;
    uint8_t out_sha[32];

    memset(sha, 0x11, sizeof(sha));
    n += pack_entry(packed + n, EOS_TLV_NONCE, &nonce, 1);
    n += pack_entry(packed + n, EOS_TLV_SHA256, sha, sizeof(sha));
    write_tlv_area(0x2000, packed, (uint16_t)n);

    ASSERT(eos_tlv_parse(&ctx, 0x2000) == EOS_OK);
    ASSERT(ctx.count == 2);

    ASSERT(eos_tlv_find(&ctx, EOS_TLV_NONCE, &entry) == EOS_OK);
    ASSERT(entry.len == 1);
    ASSERT(eos_tlv_read_data(&ctx, &entry, &out_nonce, 1) == EOS_OK);
    ASSERT(out_nonce == 0x42);

    ASSERT(eos_tlv_find(&ctx, EOS_TLV_SHA256, &entry) == EOS_OK);
    ASSERT(eos_tlv_read_data(&ctx, &entry, out_sha, sizeof(out_sha)) == EOS_OK);
    ASSERT(memcmp(out_sha, sha, sizeof(sha)) == 0);
}

TEST(test_truncated_entry_is_skipped)
{
    eos_tlv_info_t info;
    eos_tlv_entry_hdr_t hdr;
    eos_tlv_ctx_t ctx;

    /* Info claims 20 bytes total; the first entry claims 100 bytes of data. */
    info.magic = EOS_TLV_INFO_MAGIC;
    info.tlv_total_len = 20;
    hdr.type = EOS_TLV_SHA256;
    hdr.len = 100;
    memcpy(&sim_flash[0x1000], &info, sizeof(info));
    memcpy(&sim_flash[0x1000 + sizeof(info)], &hdr, sizeof(hdr));

    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_OK);
    ASSERT(ctx.count == 0);
}

TEST(test_find_and_read_reject_nulls)
{
    uint8_t sha[32];
    uint8_t packed[64];
    size_t n = 0;
    eos_tlv_ctx_t ctx;
    eos_tlv_parsed_entry_t entry;
    uint8_t out[32];

    memset(sha, 0x22, sizeof(sha));
    n += pack_entry(packed + n, EOS_TLV_SHA256, sha, sizeof(sha));
    write_tlv_area(0x1000, packed, (uint16_t)n);
    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_OK);
    ASSERT(eos_tlv_find(&ctx, EOS_TLV_SHA256, &entry) == EOS_OK);

    ASSERT(eos_tlv_find(NULL, EOS_TLV_SHA256, &entry) == EOS_ERR_INVALID);
    ASSERT(eos_tlv_find(&ctx, EOS_TLV_SHA256, NULL) == EOS_ERR_INVALID);
    ASSERT(eos_tlv_read_data(NULL, &entry, out, sizeof(out)) == EOS_ERR_INVALID);
    ASSERT(eos_tlv_read_data(&ctx, NULL, out, sizeof(out)) == EOS_ERR_INVALID);
    ASSERT(eos_tlv_read_data(&ctx, &entry, NULL, sizeof(out)) == EOS_ERR_INVALID);
}

TEST(test_read_data_rejects_small_buffer)
{
    uint8_t sha[32];
    uint8_t packed[64];
    size_t n = 0;
    eos_tlv_ctx_t ctx;
    eos_tlv_parsed_entry_t entry;
    uint8_t too_small[8];

    memset(sha, 0x33, sizeof(sha));
    n += pack_entry(packed + n, EOS_TLV_SHA256, sha, sizeof(sha));
    write_tlv_area(0x1000, packed, (uint16_t)n);
    ASSERT(eos_tlv_parse(&ctx, 0x1000) == EOS_OK);
    ASSERT(eos_tlv_find(&ctx, EOS_TLV_SHA256, &entry) == EOS_OK);
    ASSERT(eos_tlv_read_data(&ctx, &entry, too_small, sizeof(too_small)) == EOS_ERR_FULL);
}

int main(void)
{
    printf("=== eBootloader: Image TLV Parser Tests ===\n\n");
    run_test_parse_null_ctx();
    run_test_parse_bad_magic_is_not_found();
    run_test_parse_oversized_tlv_is_invalid();
    run_test_parse_undersized_tlv_is_invalid();
    run_test_parse_and_find_two_entries();
    run_test_odd_length_entry_alignment();
    run_test_truncated_entry_is_skipped();
    run_test_find_and_read_reject_nulls();
    run_test_read_data_rejects_small_buffer();
    tests_run = 9;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
