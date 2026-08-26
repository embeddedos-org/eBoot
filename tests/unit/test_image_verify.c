// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC 15288:2023

/**
 * @file test_image_verify.c
 * @brief Host tests for image header parse bounds
 */

#include "eos_image.h"
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

static void write_header(uint32_t addr, const eos_image_header_t *hdr)
{
    memcpy(&sim_flash[addr], hdr, sizeof(*hdr));
}

static void fill_valid_header(eos_image_header_t *hdr)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = EOS_IMG_MAGIC;
    hdr->hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr->hdr_size = (uint16_t)sizeof(eos_image_header_t);
    hdr->image_size = 0x1000;
    hdr->load_addr = 0x20000000;
    hdr->entry_addr = 0x20000100;
}

TEST(test_parse_valid_header)
{
    eos_image_header_t hdr, out;
    fill_valid_header(&hdr);
    write_header(0x1000, &hdr);
    ASSERT(eos_image_parse_header(0x1000, &out) == EOS_OK);
    ASSERT(out.entry_addr == 0x20000100);
}

TEST(test_parse_null_out)
{
    ASSERT(eos_image_parse_header(0x1000, NULL) == EOS_ERR_INVALID);
}

TEST(test_parse_bad_magic)
{
    eos_image_header_t hdr, out;
    fill_valid_header(&hdr);
    hdr.magic = 0xDEADBEEF;
    write_header(0x1000, &hdr);
    ASSERT(eos_image_parse_header(0x1000, &out) == EOS_ERR_NO_IMAGE);
}

TEST(test_parse_entry_outside_image)
{
    eos_image_header_t hdr, out;
    fill_valid_header(&hdr);
    hdr.entry_addr = 0x20002000; /* image is [0x20000000, 0x20001000) */
    write_header(0x1000, &hdr);
    ASSERT(eos_image_parse_header(0x1000, &out) == EOS_ERR_INVALID);
}

TEST(test_parse_load_plus_size_overflow)
{
    /* load_addr near UINT32_MAX + image_size wraps the old end-address add. */
    eos_image_header_t hdr, out;
    fill_valid_header(&hdr);
    hdr.load_addr = 0xFFFFF000u;
    hdr.image_size = 0x2000; /* 0xFFFFF000 + 0x2000 wraps */
    hdr.entry_addr = 0xFFFFF100u;
    write_header(0x1000, &hdr);
    ASSERT(eos_image_parse_header(0x1000, &out) == EOS_ERR_INVALID);
}

TEST(test_verify_integrity_without_tlv)
{
    eos_image_header_t hdr;
    uint32_t base = 0x1000;
    uint32_t payload_addr;
    uint32_t crc;
    uint8_t payload[64];

    fill_valid_header(&hdr);
    hdr.image_size = sizeof(payload);
    hdr.flags = 0; /* CRC32 path */
    payload_addr = base + hdr.hdr_size;
    memset(payload, 0xA5, sizeof(payload));
    memcpy(&sim_flash[payload_addr], payload, sizeof(payload));
    crc = eos_crc32(payload_addr, sizeof(payload));
    memcpy(hdr.hash, &crc, sizeof(crc));
    write_header(base, &hdr);

    ASSERT(eos_image_verify_integrity(&hdr, base) == EOS_OK);
}

TEST(test_verify_integrity_skips_tlv_area)
{
    /* Layout used by tools/eos_sign.py: [header][TLV][payload].
     * Hash/CRC covers payload only. Hashing from hdr_size would mix in TLV. */
    eos_image_header_t hdr;
    eos_tlv_info_t info;
    uint32_t base = 0x1000;
    uint32_t tlv_addr;
    uint16_t tlv_len = 16;
    uint32_t payload_addr;
    uint32_t crc;
    uint8_t payload[64];

    fill_valid_header(&hdr);
    hdr.image_size = sizeof(payload);
    hdr.flags = 0;
    tlv_addr = base + hdr.hdr_size;
    payload_addr = tlv_addr + tlv_len;

    memset(&info, 0, sizeof(info));
    info.magic = EOS_TLV_INFO_MAGIC;
    info.tlv_total_len = tlv_len;
    memcpy(&sim_flash[tlv_addr], &info, sizeof(info));
    memset(&sim_flash[tlv_addr + sizeof(info)], 0x11, tlv_len - sizeof(info));

    memset(payload, 0xA5, sizeof(payload));
    memcpy(&sim_flash[payload_addr], payload, sizeof(payload));
    crc = eos_crc32(payload_addr, sizeof(payload));
    memcpy(hdr.hash, &crc, sizeof(crc));
    write_header(base, &hdr);

    ASSERT(eos_image_verify_integrity(&hdr, base) == EOS_OK);
}

TEST(test_verify_integrity_rejects_corrupt_tlv)
{
    eos_image_header_t hdr;
    eos_tlv_info_t info;
    uint32_t base = 0x1000;
    uint32_t tlv_addr;
    uint8_t payload[16];
    uint32_t crc;

    fill_valid_header(&hdr);
    hdr.image_size = sizeof(payload);
    hdr.flags = 0;
    tlv_addr = base + hdr.hdr_size;

    memset(&info, 0, sizeof(info));
    info.magic = EOS_TLV_INFO_MAGIC;
    info.tlv_total_len = EOS_TLV_MAX_SIZE + 1; /* illegal */
    memcpy(&sim_flash[tlv_addr], &info, sizeof(info));

    memset(payload, 0x5A, sizeof(payload));
    memcpy(&sim_flash[tlv_addr + sizeof(info)], payload, sizeof(payload));
    crc = eos_crc32(tlv_addr + sizeof(info), sizeof(payload));
    memcpy(hdr.hash, &crc, sizeof(crc));
    write_header(base, &hdr);

    ASSERT(eos_image_verify_integrity(&hdr, base) == EOS_ERR_INVALID);
}

int main(void)
{
    printf("=== eBootloader: Image Header Parse Tests ===\n\n");
    run_test_parse_valid_header();
    run_test_parse_null_out();
    run_test_parse_bad_magic();
    run_test_parse_entry_outside_image();
    run_test_parse_load_plus_size_overflow();
    run_test_verify_integrity_without_tlv();
    run_test_verify_integrity_skips_tlv_area();
    run_test_verify_integrity_rejects_corrupt_tlv();
    tests_run = 8;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
