// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC 15288:2023

/**
 * @file test_secure_boot.c
 * @brief Host tests for the secure boot policy gates.
 *
 * The image the require_encryption policy exists to reject is a plaintext
 * one, so that is the case worth pinning.
 */

#include "eos_secure_boot.h"
#include "eos_image.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SIM_FLASH_SIZE  (64 * 1024)
#define IMAGE_ADDR      0x4000u
#define PAYLOAD_LEN     0x100u

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

/* A CRC32-checksummed image the earlier gates accept, so a test reaches the
 * encryption gate rather than stopping at integrity. */
static void write_image(uint32_t flags)
{
    eos_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = EOS_IMG_MAGIC;
    hdr.hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size    = (uint16_t)sizeof(eos_image_header_t);
    hdr.image_size  = PAYLOAD_LEN;
    hdr.load_addr   = 0x20000000;
    hdr.entry_addr  = 0x20000000;
    hdr.flags       = flags;

    uint32_t payload_addr = IMAGE_ADDR + hdr.hdr_size;
    memset(&sim_flash[payload_addr], 0xA5, PAYLOAD_LEN);

    memcpy(&sim_flash[IMAGE_ADDR], &hdr, sizeof(hdr));

    /* eos_crc32 needs the HAL live, and it is by the time this runs. */
    uint32_t crc = eos_crc32(payload_addr, PAYLOAD_LEN);
    memcpy(((eos_image_header_t *)&sim_flash[IMAGE_ADDR])->hash,
           &crc, sizeof(crc));
}

static eos_secure_boot_config_t base_cfg(void)
{
    eos_secure_boot_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.image_addr        = IMAGE_ADDR;
    cfg.require_signature = false;
    cfg.lock_debug        = false;
    return cfg;
}

TEST(test_plaintext_image_rejected_when_encryption_required)
{
    write_image(0);                     /* no EOS_IMG_FLAG_ENCRYPTED */
    eos_secure_boot_config_t cfg = base_cfg();
    cfg.require_encryption = true;

    uint32_t entry = 0;
    eos_secure_boot_result_t rc = eos_secure_boot(&cfg, &entry);

    /* The policy says encrypted only. A plaintext image must not boot. */
    ASSERT(rc == EOS_SBOOT_ERR_DECRYPT);
}

TEST(test_encrypted_image_rejected_while_decrypt_unimplemented)
{
    write_image(EOS_IMG_FLAG_ENCRYPTED);
    eos_secure_boot_config_t cfg = base_cfg();
    cfg.require_encryption = true;

    uint32_t entry = 0;
    eos_secure_boot_result_t rc = eos_secure_boot(&cfg, &entry);

    /* Decryption is not implemented, so this cannot be verified either. */
    ASSERT(rc == EOS_SBOOT_ERR_DECRYPT);
}

TEST(test_plaintext_image_boots_when_encryption_not_required)
{
    write_image(0);
    eos_secure_boot_config_t cfg = base_cfg();
    cfg.require_encryption = false;

    uint32_t entry = 0;
    eos_secure_boot_result_t rc = eos_secure_boot(&cfg, &entry);

    ASSERT(rc == EOS_SBOOT_OK);
}

TEST(test_decrypt_failure_is_attested)
{
    write_image(0);
    eos_secure_boot_config_t cfg = base_cfg();
    cfg.require_encryption = true;
    cfg.enable_attestation = true;

    uint32_t entry = 0;
    (void)eos_secure_boot(&cfg, &entry);

    /* Every other failure path records the outcome; this one must too. */
    const eos_attest_log_t *log = eos_secure_boot_get_attestation();
    ASSERT(log != NULL);
    ASSERT(log->count > 0);
    ASSERT(log->entries[log->count - 1].verify_result == EOS_SBOOT_ERR_DECRYPT);
}

int main(void)
{
    printf("Secure boot policy tests\n");
    run_test_plaintext_image_rejected_when_encryption_required();
    run_test_encrypted_image_rejected_while_decrypt_unimplemented();
    run_test_plaintext_image_boots_when_encryption_not_required();
    run_test_decrypt_failure_is_attested();
    /* Compare, and let the exit code carry it. `return 0` meant a suite that
     * ran nothing at all still reported success -- the ASSERT macro exits on
     * failure, so the only thing this return could ever have signalled is
     * exactly the case it ignored. */
    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
