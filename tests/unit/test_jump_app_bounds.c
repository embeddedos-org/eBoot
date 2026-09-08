// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_jump_app_bounds.c
 * @brief Regression test: eboot_jump_to_app() must reject an image whose
 *        payload does not fit inside the selected application slot.
 *
 * The test exercises eboot_jump_to_app() directly. An oversized image must
 * be rejected before eos_image_verify_integrity() reads any payload bytes.
 * An in-bounds image must still reach the integrity check.
 */

#include "eos_bootctl.h"
#include "eos_image.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int eboot_jump_to_app(eos_bootctl_t *bctl, eos_slot_t slot);

#define SIM_FLASH_SIZE  (128 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];
static size_t payload_bytes_read = 0;

#define SLOT_A_ADDR  0x4000u
#define SLOT_A_SIZE  0x8000u /* 32KB */
#define SLOT_B_ADDR  0x14000u
#define SLOT_B_SIZE  0x8000u /* 32KB */

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE)
        return EOS_ERR_FLASH;

    memcpy(buf, &sim_flash[addr], len);

    if (addr >= SLOT_A_ADDR + sizeof(eos_image_header_t) &&
        addr < SLOT_B_ADDR)
        payload_bytes_read += len;

    return EOS_OK;
}

static int sim_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE)
        return EOS_ERR_FLASH;

    memcpy(&sim_flash[addr], buf, len);
    return EOS_OK;
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE)
        return EOS_ERR_FLASH;

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
    .slot_a_size         = SLOT_A_SIZE,
    .slot_b_addr         = SLOT_B_ADDR,
    .slot_b_size         = SLOT_B_SIZE,
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
        payload_bytes_read = 0; \
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

static void write_header(uint32_t addr, const eos_image_header_t *hdr)
{
    memcpy(&sim_flash[addr], hdr, sizeof(*hdr));
}

static void fill_header(eos_image_header_t *hdr, uint32_t image_size)
{
    memset(hdr, 0, sizeof(*hdr));

    hdr->magic = EOS_IMG_MAGIC;
    hdr->hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr->hdr_size = (uint16_t)sizeof(eos_image_header_t);
    hdr->image_size = image_size;
    hdr->load_addr = 0x20000000;
    hdr->entry_addr = 0x20000100;

    /* flags == 0 selects the CRC32 integrity path. */
}

TEST(test_oversized_image_rejected_before_reading_payload)
{
    eos_image_header_t hdr;
    eos_bootctl_t bctl;

    fill_header(&hdr, SLOT_A_SIZE + 0x4000u);
    write_header(SLOT_A_ADDR, &hdr);
    memset(&bctl, 0, sizeof(bctl));

    int rc = eboot_jump_to_app(&bctl, EOS_SLOT_A);

    ASSERT(rc == EOS_ERR_INVALID);
    ASSERT(payload_bytes_read == 0);
}

TEST(test_in_bounds_image_reaches_integrity_check)
{
    uint32_t image_size =
        SLOT_A_SIZE - sizeof(eos_image_header_t) - 0x100u;

    eos_image_header_t hdr;
    eos_bootctl_t bctl;

    fill_header(&hdr, image_size);
    write_header(SLOT_A_ADDR, &hdr);
    memset(&bctl, 0, sizeof(bctl));

    (void)eboot_jump_to_app(&bctl, EOS_SLOT_A);

    /*
     * The image has no valid CRC/signature, so the function is expected
     * to fail later. What matters here is that the new slot-size check
     * does not reject it before the payload is read.
     */
    ASSERT(payload_bytes_read == image_size);
}

int main(void)
{
    printf("=== eBootloader: Jump-App Slot-Size Bounds Tests ===\n\n");

    run_test_oversized_image_rejected_before_reading_payload();
    run_test_in_bounds_image_reaches_integrity_check();

    tests_run = 2;

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
