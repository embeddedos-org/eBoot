// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_slot_size_bounds.c
 * @brief Regression test: verify_slot() must reject an image_size larger
 *        than the slot's real capacity, instead of streaming reads past
 *        the slot boundary during eos_image_verify_integrity().
 *
 * eos_image_parse_header() only bounds image_size against a fixed 16MB
 * ceiling; it has no notion of the board's actual slot size. Before this
 * fix, slot_manager.c's verify_slot() would happily hand an oversized (but
 * <16MB) image_size straight to eos_image_verify_integrity(), which reads
 * `image_size` bytes starting right after the header -- past the slot,
 * into whatever flash follows -- on every boot scan.
 *
 * This test proves the fix by instrumenting the simulated flash read and
 * checking how many payload bytes verify_slot() actually attempts to read:
 *   - an oversized image must be rejected (state == INVALID) BEFORE any
 *     attempt to read its payload;
 *   - an image that genuinely fits the slot must still be let through to
 *     the integrity check (proving the fix doesn't over-reject legitimate
 *     images) -- it reads the real payload size and then fails later on
 *     the (deliberately absent) signature, which is expected and outside
 *     this test's scope.
 */

#include "eos_slot_manager.h"
#include "eos_image.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SIM_FLASH_SIZE  (128 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];
static size_t payload_bytes_read = 0; /* bytes read at/after the header region */

#define SLOT_A_ADDR  0x4000u
#define SLOT_A_SIZE  0x8000u /* 32KB */
#define SLOT_B_ADDR  0x14000u
#define SLOT_B_SIZE  0x8000u /* 32KB */

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr], len);
    /* Count only reads that land inside slot A's own payload region (past
     * its header, before slot B begins), so neither slot A's header read
     * nor slot B's independent header-parse attempt skew the counter. */
    if (addr >= SLOT_A_ADDR + sizeof(eos_image_header_t) && addr < SLOT_B_ADDR)
        payload_bytes_read += len;
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
    /* flags left at 0 -> CRC32 fallback path in eos_image_verify_integrity,
     * not SHA-256, so this test needs no crypto keystore setup. */
}

TEST(test_oversized_image_rejected_before_reading_payload)
{
    /* image_size fits well under the global 16MB ceiling in
     * eos_image_parse_header(), but is larger than the real 32KB slot. */
    eos_image_header_t hdr;
    fill_header(&hdr, SLOT_A_SIZE + 0x4000u /* 16KB over the slot */);
    write_header(SLOT_A_ADDR, &hdr);

    eos_slot_scan_all();

    ASSERT(eos_slot_get_state(EOS_SLOT_A) == EOS_SLOT_STATE_INVALID);
    /* The fix must reject before eos_image_verify_integrity() ever streams
     * the (oversized, out-of-slot) payload through eos_crc32(). */
    ASSERT(payload_bytes_read == 0);
}

TEST(test_in_bounds_image_is_not_over_rejected)
{
    /* image_size that genuinely fits inside the slot must NOT be blocked
     * by the new size check -- it should be let through to the integrity
     * check (which will read the full payload, then fail later on the
     * absent signature -- expected, and out of scope for this test). */
    uint32_t image_size = SLOT_A_SIZE - sizeof(eos_image_header_t) - 0x100u;
    eos_image_header_t hdr;
    fill_header(&hdr, image_size);
    write_header(SLOT_A_ADDR, &hdr);

    eos_slot_scan_all();

    /* Not VALID (no real signature was provided), but the size check let
     * it proceed far enough to actually attempt reading the real payload,
     * proving legitimate, in-slot images are not caught by this fix. */
    ASSERT(payload_bytes_read == image_size);
}

int main(void)
{
    printf("=== eBootloader: Slot-Size Bounds Regression Tests ===\n\n");
    run_test_oversized_image_rejected_before_reading_payload();
    run_test_in_bounds_image_is_not_over_rejected();
    tests_run = 2;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
