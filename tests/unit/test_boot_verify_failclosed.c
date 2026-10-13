// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC 15288:2023

/**
 * @file test_boot_verify_failclosed.c
 * @brief Fail-closed negative tests for the boot verification path.
 *
 * The positive paths are covered elsewhere (test_image_verify.c,
 * test_ed25519.c, test_secure_boot.c). This suite pins the refusal
 * side of the decision that actually boots an image:
 * eos_image_verify_signature() and eos_secure_boot().
 *
 * The fixture is a real Ed25519-signed image: header + 256-byte
 * payload, signed over the 92-byte header prefix (EOS_IMG_SIGNED_LEN)
 * with the RFC 8032 TEST 1 key — the keystore's compiled-in
 * development key — so the valid image verifies against the key the
 * bootloader itself selects when no OTP is provisioned. The
 * wrong-key fixture is the same prefix signed with RFC 8032 TEST 2.
 *
 * Every negative case asserts the boot-level contract, not just an
 * error code: eos_secure_boot() returns non-OK and leaves entry_out
 * at its sentinel — boot REFUSES, it does not merely log and jump.
 *
 * One refusal is by design and worth pinning as such: with
 * require_signature=true a valid image still does not boot on an
 * unprovisioned-anchor configuration, because step 4 of
 * eos_secure_boot() cannot compare the image key hash against an
 * OTP anchor that could not be read — it fails closed
 * (EOS_SBOOT_ERR_SIGNATURE) until the TLV key-hash comparison lands.
 */

#include "eos_secure_boot.h"
#include "eos_image.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SIM_FLASH_SIZE  (64 * 1024)
#define IMAGE_ADDR      0x4000u
#define ENTRY_SENTINEL  0xDEADBEEFu

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
        stage_valid_image(); \
        printf("  %-50s ", #name); \
        tests_run++; \
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

/* ---- Signed fixture (generated; see file header) ---- */

static const uint8_t k_signed_image[412] = {
    0x49, 0x53, 0x4f, 0x45, 0x02, 0x00, 0x9c, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1f, 0x5f, 0x72, 0xa0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8a, 0xb5, 0xd3, 0x50,
    0x83, 0x17, 0x5c, 0x7b, 0x50, 0x13, 0xdf, 0xfe, 0xf4, 0x3e, 0x41, 0x36,
    0xc8, 0x0f, 0x17, 0xdd, 0x12, 0xe3, 0xdc, 0x97, 0x9f, 0x1f, 0x82, 0xbb,
    0xcc, 0x7a, 0xd1, 0x87, 0xb0, 0x7c, 0x71, 0xa9, 0xb0, 0x92, 0x9a, 0x0c,
    0x92, 0xa2, 0xfe, 0xf0, 0x5c, 0xf2, 0x43, 0x96, 0x10, 0xf8, 0x88, 0xb6,
    0x8c, 0xa9, 0x78, 0x56, 0xb3, 0x40, 0x46, 0x39, 0x5c, 0xac, 0x15, 0x05,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    0xa5, 0xa5, 0xa5, 0xa5,
};

static const uint8_t k_wrong_key_signature[64] = {
    0x5f, 0xe8, 0x1d, 0x03, 0xc5, 0x87, 0xa6, 0xd4, 0xde, 0xff, 0x12, 0x52,
    0x30, 0xc2, 0x41, 0x9c, 0xab, 0x14, 0x03, 0xd4, 0x6c, 0x28, 0x45, 0x97,
    0x65, 0xa0, 0x8e, 0x58, 0xa2, 0x65, 0x64, 0xe0, 0x27, 0xa7, 0x8c, 0xdc,
    0x63, 0x20, 0x6b, 0x1e, 0xf5, 0xf5, 0x03, 0xfc, 0x32, 0x4e, 0x96, 0x68,
    0x62, 0xc3, 0x17, 0x22, 0xcb, 0x17, 0x28, 0xe3, 0x3b, 0x4a, 0x12, 0x2f,
    0x86, 0x6c, 0x8f, 0x05,
};

#define SIGNED_IMAGE_LEN  (sizeof(k_signed_image))
#define SIG_OFFSET        (IMAGE_ADDR + 92) /* offsetof(header, signature) */

static void stage_valid_image(void)
{
    memcpy(&sim_flash[IMAGE_ADDR], k_signed_image, SIGNED_IMAGE_LEN);
}

static eos_secure_boot_config_t signed_cfg(void)
{
    eos_secure_boot_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.image_addr        = IMAGE_ADDR;
    cfg.require_signature = true;
    cfg.lock_debug        = false;
    return cfg;
}

static void parse_staged(eos_image_header_t *hdr)
{
    ASSERT(eos_image_parse_header(IMAGE_ADDR, hdr) == EOS_OK);
}

/* Control: the valid signed image verifies at the entry point the
 * boot chain calls, and its integrity check passes. Without this,
 * the negative cases below could be refusing a broken fixture. */
TEST(test_valid_signed_image_verifies)
{
    eos_image_header_t hdr;
    parse_staged(&hdr);
    ASSERT(eos_image_verify_integrity(&hdr, IMAGE_ADDR) == EOS_OK);
    ASSERT(eos_image_verify_signature(&hdr) == EOS_OK);
}

/* Control at boot level: the same image boots when signature
 * enforcement is off, so the harness can boot a good image. */
TEST(test_valid_image_boots_as_control)
{
    eos_secure_boot_config_t cfg = signed_cfg();
    cfg.require_signature = false;

    uint32_t entry = ENTRY_SENTINEL;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_OK);
    ASSERT(entry == 0x20000000u);
}

/* Corrupt signature: one flipped byte must refuse the boot. */
TEST(test_corrupt_signature_refused)
{
    sim_flash[SIG_OFFSET] ^= 0x01;

    eos_image_header_t hdr;
    parse_staged(&hdr);
    ASSERT(eos_image_verify_signature(&hdr) == EOS_ERR_SIGNATURE);

    eos_secure_boot_config_t cfg = signed_cfg();
    uint32_t entry = ENTRY_SENTINEL;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_ERR_SIGNATURE);
    ASSERT(entry == ENTRY_SENTINEL);
}

/* Wrong key: a well-formed signature from a different key must
 * refuse the boot — the failure is the key, not the encoding. */
TEST(test_wrong_key_signature_refused)
{
    memcpy(&sim_flash[SIG_OFFSET], k_wrong_key_signature,
           sizeof(k_wrong_key_signature));

    eos_image_header_t hdr;
    parse_staged(&hdr);
    ASSERT(eos_image_verify_signature(&hdr) == EOS_ERR_SIGNATURE);

    eos_secure_boot_config_t cfg = signed_cfg();
    uint32_t entry = ENTRY_SENTINEL;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_ERR_SIGNATURE);
    ASSERT(entry == ENTRY_SENTINEL);
}

/* Truncated signature: sig_len below the Ed25519 size must refuse. */
TEST(test_truncated_signature_refused)
{
    sim_flash[IMAGE_ADDR + 61] = 32; /* sig_len field */

    eos_image_header_t hdr;
    parse_staged(&hdr);
    ASSERT(eos_image_verify_signature(&hdr) == EOS_ERR_SIGNATURE);

    eos_secure_boot_config_t cfg = signed_cfg();
    uint32_t entry = ENTRY_SENTINEL;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_ERR_SIGNATURE);
    ASSERT(entry == ENTRY_SENTINEL);
}

/* Truncated image: payload bytes missing/corrupted after signing
 * must refuse at the integrity stage, before any jump. */
TEST(test_truncated_image_refused)
{
    memset(&sim_flash[IMAGE_ADDR + 156 + 128], 0x00, 128);

    eos_secure_boot_config_t cfg = signed_cfg();
    cfg.require_signature = false; /* isolate the integrity gate */
    uint32_t entry = ENTRY_SENTINEL;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_ERR_INTEGRITY);
    ASSERT(entry == ENTRY_SENTINEL);
}

/* Fail-closed by design: even the valid signed image does not boot
 * with require_signature=true while the OTP key-hash comparison
 * (step 4) cannot run — an unverifiable check refuses, never passes. */
TEST(test_signature_required_fails_closed_without_anchor)
{
    eos_secure_boot_config_t cfg = signed_cfg();
    uint32_t entry = ENTRY_SENTINEL;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_ERR_SIGNATURE);
    ASSERT(entry == ENTRY_SENTINEL);
}

int main(void)
{
    printf("Boot verification fail-closed tests\n");
    run_test_valid_signed_image_verifies();
    run_test_valid_image_boots_as_control();
    run_test_corrupt_signature_refused();
    run_test_wrong_key_signature_refused();
    run_test_truncated_signature_refused();
    run_test_truncated_image_refused();
    run_test_signature_required_fails_closed_without_anchor();
    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
