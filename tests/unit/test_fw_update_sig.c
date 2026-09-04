// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_fw_update_sig.c
 * @brief The firmware-update install path must authenticate, not just hash.
 *
 * eos_fw_update_finalize() streams an attacker-supplied image into a slot and
 * then decides whether to commit it. Signature verification there must not be
 * gated on the image header's own sig_type field — that field is under the
 * sender's control, so an image declaring EOS_SIG_NONE/CRC32/SHA256 would
 * otherwise skip verification and be installed unsigned. Integrity (SHA-256 or
 * CRC32) only proves the payload matches a hash the same sender put in the
 * header; it is not authentication.
 *
 * These tests drive the real pipeline (begin -> write -> finalize) over a
 * simulated flash and assert that an unsigned image is rejected specifically at
 * the signature stage, while a corrupt one is still rejected earlier at the
 * integrity stage — so the signature gate is doing the work, not the hash.
 */

#include "eos_fw_update.h"
#include "eos_image.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Simulated flash + board ---- */

#define SIM_FLASH_SIZE   (128 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];

#define SIM_SLOT_A_ADDR  0x00000
#define SIM_SLOT_A_SIZE  0x08000
#define SIM_SLOT_B_ADDR  0x10000
#define SIM_SLOT_B_SIZE  0x08000

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

static uint32_t sim_tick_val = 0;
static uint32_t sim_get_tick(void) { return sim_tick_val++; }
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t x) { (void)x; }
static void sim_jump(uint32_t addr) { (void)addr; }
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static bool sim_recovery_pin(void) { return false; }
static void sim_system_reset(void) {}
static int sim_uart_init(uint32_t baud) { (void)baud; return EOS_OK; }
static int sim_uart_send(const void *buf, size_t len) { (void)buf; (void)len; return EOS_OK; }
static int sim_uart_recv(void *buf, size_t len, uint32_t t) { (void)buf; (void)len; (void)t; return EOS_ERR_TIMEOUT; }

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size          = SIM_FLASH_SIZE,
    .slot_a_addr         = SIM_SLOT_A_ADDR,
    .slot_a_size         = SIM_SLOT_A_SIZE,
    .slot_b_addr         = SIM_SLOT_B_ADDR,
    .slot_b_size         = SIM_SLOT_B_SIZE,
    .recovery_addr       = 0,
    .recovery_size       = 0,
    .bootctl_addr        = 0x1000,
    .bootctl_backup_addr = 0x2000,
    .log_addr            = 0x3000,
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
    .uart_init           = sim_uart_init,
    .uart_send           = sim_uart_send,
    .uart_recv           = sim_uart_recv,
    .get_tick_ms         = sim_get_tick,
    .disable_interrupts  = sim_noop,
    .enable_interrupts   = sim_noop,
    .deinit_peripherals  = sim_noop,
};

static void setup(void)
{
    memset(sim_flash, 0xFF, sizeof(sim_flash));
    sim_tick_val = 0;
    eos_hal_init(&sim_ops);
}

/* ---- Test harness (matches the other suites in tests/unit) ---- */

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        setup(); \
        printf("  %-58s ", #name); \
        name(); \
        tests_passed++; \
        printf("[OK]\n"); \
    } \
    static void name(void)

/* ---- Image construction ---- */

#define PAYLOAD_LEN 64

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

/* Build a CRC32-integrity image (flags = 0) into out[]; returns total length.
 * sig_type is caller-chosen; corrupt_crc flips the stored CRC so the image
 * fails the integrity stage instead of reaching the signature stage. */
static size_t build_image(uint8_t *out, uint8_t sig_type, int corrupt_crc)
{
    eos_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = EOS_IMG_MAGIC;
    hdr.hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size    = (uint16_t)sizeof(hdr);
    hdr.image_size  = PAYLOAD_LEN;
    hdr.load_addr   = SIM_SLOT_B_ADDR;
    hdr.entry_addr  = SIM_SLOT_B_ADDR;
    hdr.flags       = 0;             /* CRC32 integrity path */
    hdr.sig_type    = sig_type;
    hdr.sig_len     = 0;

    uint8_t payload[PAYLOAD_LEN];
    for (int i = 0; i < PAYLOAD_LEN; i++)
        payload[i] = (uint8_t)(0xA0 + (i & 0x0F));

    uint32_t crc = crc32_calc(payload, PAYLOAD_LEN);
    if (corrupt_crc) crc ^= 0xFFFFFFFFu;
    memcpy(hdr.hash, &crc, sizeof(crc));

    memcpy(out, &hdr, sizeof(hdr));
    memcpy(out + sizeof(hdr), payload, PAYLOAD_LEN);
    return sizeof(hdr) + PAYLOAD_LEN;
}

/* Stream one image through begin -> write -> finalize; return finalize rc
 * (or the begin/write rc if an earlier stage fails). */
static int install(uint8_t sig_type, int corrupt_crc, eos_fw_update_ctx_t *ctx)
{
    uint8_t img[sizeof(eos_image_header_t) + PAYLOAD_LEN];
    size_t n = build_image(img, sig_type, corrupt_crc);

    int rc = eos_fw_update_begin(ctx, EOS_SLOT_B);
    if (rc != EOS_OK) return rc;

    rc = eos_fw_update_write(ctx, img, n);
    if (rc != EOS_OK) return rc;

    return eos_fw_update_finalize(ctx, EOS_UPGRADE_TEST);
}

/* ================================================================ */

/*
 * The core regression. An image that declares itself unsigned (EOS_SIG_NONE)
 * but carries a correct CRC used to be installed with no signature check: the
 * finalize guard was `sig_type >= EOS_SIG_ED25519`, and NONE (0) fails it. It
 * must now be rejected at the signature stage.
 */
TEST(test_unsigned_image_with_valid_crc_is_rejected)
{
    eos_fw_update_ctx_t ctx;
    int rc = install(EOS_SIG_NONE, 0, &ctx);

    ASSERT(rc == EOS_ERR_SIGNATURE);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_ERROR);
}

/*
 * A sig_type of EOS_SIG_SHA256 (2) is also below EOS_SIG_ED25519, so it took
 * the same bypass. The whole NONE/CRC32/SHA256 class must be refused.
 */
TEST(test_sha256_sigtype_is_still_unsigned_and_rejected)
{
    eos_fw_update_ctx_t ctx;
    int rc = install(EOS_SIG_SHA256, 0, &ctx);

    ASSERT(rc == EOS_ERR_SIGNATURE);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_ERROR);
}

/*
 * Control: prove the rejection above is the signature gate, not the integrity
 * gate. The same unsigned image with a corrupted CRC is rejected earlier, with
 * EOS_ERR_CRC — so an unsigned image that passes integrity is stopped
 * specifically by the signature check, exactly the property being fixed.
 */
TEST(test_corrupt_image_is_rejected_at_integrity_stage)
{
    eos_fw_update_ctx_t ctx;
    int rc = install(EOS_SIG_NONE, 1, &ctx);

    ASSERT(rc == EOS_ERR_CRC);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_ERROR);
}

int main(void)
{
    printf("=== eBootloader: FW Update Signature-Gate Tests ===\n\n");

    run_test_unsigned_image_with_valid_crc_is_rejected();
    run_test_sha256_sigtype_is_still_unsigned_and_rejected();
    run_test_corrupt_image_is_rejected_at_integrity_stage();

    tests_run = 3;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
