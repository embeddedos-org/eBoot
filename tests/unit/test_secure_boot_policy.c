// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_secure_boot_policy.c
 * @brief The debug-lock policy must be enforced, not merely attempted
 *
 * cfg.lock_debug asks for SWD/JTAG to be closed before the image runs.
 * eos_secure_boot_lock_debug() writes an OTP fuse to do that, and its result
 * used to be discarded -- so a board whose OTP write failed, or one with no
 * otp_write at all, booted with the debug port open while attestation recorded
 * EOS_SBOOT_OK.
 *
 * Kept separate from tests/unit/test_secure_boot.c (added by #72) so the two
 * do not collide.
 */

#include "eos_secure_boot.h"
#include "eos_hal.h"
#include "eos_image.h"
#include "eos_crypto_boot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("  %-54s ", #name); \
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

/* ---- Simulated board: OTP only, with a scriptable write result ---- */

#define OTP_SIZE 0x400
/* Mirrors core/secure_boot.c; the anchor lives at a fixed OTP offset. */
#define OTP_KEY_HASH_OFFSET 0x100
#define FLASH_BASE 0x08000000U
#define FLASH_SIZE 0x800
static uint8_t sim_flash[FLASH_SIZE];
static uint8_t sim_otp[OTP_SIZE];
static int otp_write_rc;
static int otp_write_calls;
static int provide_otp_write;

static int sim_otp_read(uint32_t offset, void *buf, size_t len)
{
    if ((uint64_t)offset + len > OTP_SIZE) return EOS_ERR_INVALID;
    memcpy(buf, sim_otp + offset, len);
    return EOS_OK;
}

static int sim_otp_write(uint32_t offset, const void *buf, size_t len)
{
    otp_write_calls++;
    if (otp_write_rc != EOS_OK) return otp_write_rc;
    if ((uint64_t)offset + len > OTP_SIZE) return EOS_ERR_INVALID;
    memcpy(sim_otp + offset, buf, len);
    return EOS_OK;
}

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr < FLASH_BASE) return EOS_ERR_INVALID;
    uint32_t off = addr - FLASH_BASE;
    if ((uint64_t)off + len > FLASH_SIZE) return EOS_ERR_INVALID;
    memcpy(buf, sim_flash + off, len);
    return EOS_OK;
}

/* An image that clears steps 1, 2 and 5, so control actually reaches the
 * debug lock in step 7. Unsigned and unencrypted -- cfg turns those steps
 * off -- because what is under test is the policy step, not the crypto. */
static void stage_bootable_image(void)
{
    memset(sim_flash, 0, sizeof(sim_flash));

    static const uint8_t payload[16] = {
        0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04,
        0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,
    };

    eos_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = EOS_IMG_MAGIC;
    hdr.hdr_version   = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size      = (uint16_t)sizeof(eos_image_header_t);
    hdr.image_size    = (uint32_t)sizeof(payload);
    hdr.load_addr     = 0x20000000U;
    hdr.entry_addr    = 0x20000001U;
    hdr.image_version = 1;
    /* Without this flag verify_integrity takes the CRC32 branch and reads a
     * CRC out of hash[], so step 2 fails and step 7 is never reached. */
    hdr.flags         = EOS_IMG_FLAG_HASH_SHA256;
    hdr.sig_type      = EOS_SIG_NONE;
    hdr.sig_len       = 0;
    hdr.tlv_len       = 0;

    eos_sha256_ctx_t sha;
    eos_sha256_init(&sha);
    eos_sha256_update(&sha, payload, sizeof(payload));
    eos_sha256_final(&sha, hdr.hash);

    memcpy(sim_flash, &hdr, sizeof(hdr));
    memcpy(sim_flash + sizeof(hdr), payload, sizeof(payload));
}

static eos_board_ops_t sim_ops;

static void reset_fixture(void)
{
    memset(&sim_ops, 0, sizeof(sim_ops));
    memset(sim_otp, 0, sizeof(sim_otp));
    sim_ops.otp_read = sim_otp_read;
    sim_ops.flash_read = sim_flash_read;
    if (provide_otp_write) sim_ops.otp_write = sim_otp_write;
    otp_write_rc = EOS_OK;
    otp_write_calls = 0;
    eos_hal_init(&sim_ops);
}

/* A working eFuse: the lock is written and reported. */
TEST(test_lock_debug_reports_success_when_the_fuse_is_written)
{
    provide_otp_write = 1;
    reset_fixture();

    ASSERT(eos_secure_boot_lock_debug() == EOS_OK);
    ASSERT(otp_write_calls == 1);
}

/* An eFuse write that fails leaves the port open. Saying so is the whole
 * point: the caller asked for it to be closed. */
TEST(test_lock_debug_reports_a_failed_fuse_write)
{
    provide_otp_write = 1;
    reset_fixture();
    otp_write_rc = EOS_ERR_FLASH;

    ASSERT(eos_secure_boot_lock_debug() != EOS_OK);
    ASSERT(otp_write_calls == 1);
}

/* The common case, and the one that used to be silent: a board that provides
 * no otp_write at all. eos_hal_otp_write() returns EOS_ERR_NOT_SUPPORTED, so
 * lock_debug: true was a no-op on every such board. */
TEST(test_lock_debug_reports_a_board_with_no_otp_write)
{
    provide_otp_write = 0;
    reset_fixture();

    ASSERT(eos_secure_boot_lock_debug() != EOS_OK);
    ASSERT(otp_write_calls == 0);
}

/* The three tests above assert what the *helper* reports. This one asserts
 * what the boot *does* with that report, which is the behaviour this PR
 * actually changed -- the early return at step 7. Without it the suite
 * covers "attempted" while the file's own docblock claims "enforced", and a
 * revert of the step-7 branch would leave every other test passing. */
TEST(test_secure_boot_refuses_when_the_debug_lock_cannot_be_taken)
{
    provide_otp_write = 0;          /* the common case: board has no otp_write */
    reset_fixture();
    stage_bootable_image();

    eos_secure_boot_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.image_addr        = FLASH_BASE;
    cfg.require_signature = false;
    cfg.require_encryption = false;
    cfg.lock_debug        = true;

    uint32_t entry = 0;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_ERR_POLICY);
}

/* The counter-check: the same image and the same board, with lock_debug off,
 * must still boot. Without this, the test above would also pass if steps 1-6
 * were failing for some unrelated reason and never reaching step 7. */
TEST(test_the_same_image_boots_when_no_debug_lock_is_asked_for)
{
    provide_otp_write = 0;
    reset_fixture();
    stage_bootable_image();

    eos_secure_boot_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.image_addr        = FLASH_BASE;
    cfg.require_signature = false;
    cfg.require_encryption = false;
    cfg.lock_debug        = false;

    uint32_t entry = 0;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_OK);
    ASSERT(entry == 0x20000001U);
}

/* And with a working fuse, lock_debug: true boots and the fuse is written. */
TEST(test_secure_boot_proceeds_when_the_debug_lock_succeeds)
{
    provide_otp_write = 1;
    reset_fixture();
    stage_bootable_image();

    eos_secure_boot_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.image_addr        = FLASH_BASE;
    cfg.require_signature = false;
    cfg.require_encryption = false;
    cfg.lock_debug        = true;

    uint32_t entry = 0;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_OK);
    ASSERT(otp_write_calls == 1);
}

/* Step 4's two new refusals have NO test here, deliberately, and this note is
 * the record of why rather than an omission to be discovered later.
 *
 * Reaching step 4 requires passing step 3, which is a real Ed25519 signature
 * over the header prefix checked against the keystore anchor. A fixture for
 * that is buildable -- the keystore ships RFC 8032 TEST 1's public key and
 * the matching private key is in the RFC -- but the machinery for it belongs
 * to #88 (tools/gen_signed_image_fixture.py), not here.
 *
 * I wrote the obvious test first and it was worthless: with
 * require_signature = true and an unsigned fixture the boot fails at step 3,
 * returning the same EOS_SBOOT_ERR_SIGNATURE that step 4 returns, so it
 * passed against the unfixed code too. Verified that by reverting step 4 and
 * watching it still pass. A test that cannot fail is worse than none, so it
 * is not in this file.
 *
 * The counter-check below is what this file can honestly assert: the change
 * does not refuse a boot it should allow.
 */
TEST(test_the_ordinary_boot_path_is_unaffected_by_the_step_4_change)
{
    provide_otp_write = 1;
    reset_fixture();                /* leaves the OTP anchor all zero */
    stage_bootable_image();

    eos_secure_boot_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.image_addr        = FLASH_BASE;
    cfg.require_signature = false;
    cfg.lock_debug        = false;

    uint32_t entry = 0;
    ASSERT(eos_secure_boot(&cfg, &entry) == EOS_SBOOT_OK);
    ASSERT(entry == 0x20000001U);
}

int main(void)
{
    printf("=== eBootloader: Secure Boot Debug-Lock Policy Tests ===\n\n");

    run_test_lock_debug_reports_success_when_the_fuse_is_written();
    run_test_lock_debug_reports_a_failed_fuse_write();
    run_test_lock_debug_reports_a_board_with_no_otp_write();
    run_test_secure_boot_refuses_when_the_debug_lock_cannot_be_taken();
    run_test_the_same_image_boots_when_no_debug_lock_is_asked_for();
    run_test_secure_boot_proceeds_when_the_debug_lock_succeeds();
    run_test_the_ordinary_boot_path_is_unaffected_by_the_step_4_change();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
