// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_secure_boot_policy.c
 * @brief The debug-lock policy must be enforced, not merely attempted
 *
 * cfg.lock_debug asks for SWD/JTAG to be closed before the image runs.
 * eos_secure_boot_lock_debug() locks through the board's HAL debug_lock
 * op and verifies the lock by read-back through debug_status, so EOS_OK
 * means the port is actually locked.
 *
 * That verification is #184. The function used to write 0xFF to an OTP
 * byte (0x1F0) and report EOS_OK: on STM32F4 an erased byte already
 * reads 0xFF, so the write was a no-op that passed step 7 while the
 * debug port stayed open, and no read-back could ever have checked it
 * (0xFF is indistinguishable from erased). The OTP write is gone; the
 * lock is the board's debug_lock op now, and the mock below models OTP
 * as erased (0xFF) so the next board to wire otp_write cannot repeat
 * the same silent pass.
 *
 * Kept separate from tests/unit/test_secure_boot.c (added by #72) so the two
 * do not collide.
 */

#include "eos_secure_boot.h"
#include "eos_debug_lock.h"
#include "eos_rollback.h"
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

/* ---- Simulated board: OTP only, with a scriptable write result ----
 *
 * The OTP window starts erased (0xFF), as real OTP does (#184): a test
 * that writes 0xFF over an erased byte must see a no-op, not a change.
 * The debug lock is a separate op pair -- debug_lock programs the lock,
 * debug_status reports what the board now reads back -- so the tests
 * below drive the lock through those ops, never through OTP. */

#define OTP_SIZE 0x400
/* Mirrors core/secure_boot.c; the anchor lives at a fixed OTP offset. */
#define OTP_KEY_HASH_OFFSET 0x100
/* The OTP byte the old lock_debug wrote (0xFF) and reported as locked. */
#define OTP_DEBUG_LOCK_OFFSET 0x1F0
#define FLASH_BASE 0x08000000U
#define FLASH_SIZE 0x800
static uint8_t sim_flash[FLASH_SIZE];
static uint8_t sim_otp[OTP_SIZE];
static int otp_write_rc;
static int otp_write_calls;
static int provide_otp_write;
static int provide_debug_lock;  /* wires both debug_lock and debug_status */
static int debug_lock_rc;
static int debug_lock_calls;
static uint32_t sim_reported_debug_status; /* what debug_status reports */
static int sim_debug_status_rc;
/* Trivial monotonic counter at floor 0, so the anti-rollback step (step 5b
 * of eos_secure_boot) passes and this suite keeps measuring the debug-lock
 * policy, not rollback. eos_rollback_verify() fails closed without one; a
 * board with no counter ops is covered by test_rollback.c instead. */
static uint32_t sim_counter = 0;
static int sim_monotonic_increment_calls;
static int sim_monotonic_increment_fails; /* if set, the burn reports failure */
static int provide_monotonic = 1;
static int sim_monotonic_read(uint32_t *value)
{
    if (!value) return EOS_ERR_INVALID;
    *value = sim_counter;
    return EOS_OK;
}
static int sim_monotonic_increment(void)
{
    sim_monotonic_increment_calls++;
    if (sim_monotonic_increment_fails) return EOS_ERR_GENERIC;
    sim_counter++;
    return EOS_OK;
}

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

static int sim_debug_lock(void)
{
    debug_lock_calls++;
    return debug_lock_rc;
}

static int sim_debug_status(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;
    if (sim_debug_status_rc != EOS_OK) return sim_debug_status_rc;
    *status = sim_reported_debug_status;
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
    /* Erased OTP reads 0xFF (#184): the fixture models the physics, so
     * a write of the erased value is observable as a no-op. */
    memset(sim_otp, 0xFF, sizeof(sim_otp));
    sim_ops.otp_read = sim_otp_read;
    sim_ops.flash_read = sim_flash_read;
    /* Step 5b (anti-rollback, #103) reads the image's TLV counter through the
     * HAL slot that contains it, and an image in no slot is a bad header
     * before step 7 is ever reached. The staged image is slot A. */
    sim_ops.slot_a_addr = FLASH_BASE;
    sim_ops.slot_a_size = FLASH_SIZE;
    if (provide_monotonic) {
        sim_ops.monotonic_read = sim_monotonic_read;
        sim_ops.monotonic_increment = sim_monotonic_increment;
    }
    sim_counter = 0;
    sim_monotonic_increment_calls = 0;
    sim_monotonic_increment_fails = 0;
    if (provide_otp_write) sim_ops.otp_write = sim_otp_write;
    if (provide_debug_lock) {
        sim_ops.debug_lock = sim_debug_lock;
        sim_ops.debug_status = sim_debug_status;
    }
    otp_write_rc = EOS_OK;
    otp_write_calls = 0;
    debug_lock_rc = EOS_OK;
    debug_lock_calls = 0;
    sim_reported_debug_status = EOS_DEBUG_STATUS_LOCKED;
    sim_debug_status_rc = EOS_OK;
    eos_hal_init(&sim_ops);
}

/* The lock is taken through the board's debug_lock op and confirmed by
 * read-back: success means the port is locked, and the OTP byte the old
 * implementation wrote is never touched -- it still reads erased. */
TEST(test_lock_debug_reports_success_when_the_port_is_locked)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();

    ASSERT(eos_secure_boot_lock_debug() == EOS_OK);
    ASSERT(debug_lock_calls == 1);
    ASSERT(otp_write_calls == 0);
    ASSERT(sim_otp[OTP_DEBUG_LOCK_OFFSET] == 0xFF);
}

/* A lock that reports failure leaves the port open. Saying so is the
 * whole point: the caller asked for it to be closed. */
TEST(test_lock_debug_reports_a_failed_lock)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();
    debug_lock_rc = EOS_ERR_FLASH;

    ASSERT(eos_secure_boot_lock_debug() != EOS_OK);
    ASSERT(debug_lock_calls == 1);
}

/* The common case, and the one that used to be silent: a board that
 * provides no debug_lock op at all. eos_hal_debug_lock() returns
 * EOS_ERR_NOT_SUPPORTED, so lock_debug: true fails closed instead of
 * passing step 7 with the port open. */
TEST(test_lock_debug_reports_a_board_with_no_debug_lock)
{
    provide_otp_write = 1;
    provide_debug_lock = 0;
    reset_fixture();

    ASSERT(eos_secure_boot_lock_debug() == EOS_ERR_NOT_SUPPORTED);
    ASSERT(debug_lock_calls == 0);
    ASSERT(otp_write_calls == 0);
}

/* The read-back is the fix: a lock that reports success but whose
 * status still reads UNLOCKED -- a fuse burn that silently did not
 * land -- must not return EOS_OK. */
TEST(test_lock_debug_fails_when_status_does_not_confirm_the_lock)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();
    sim_reported_debug_status = EOS_DEBUG_STATUS_UNLOCKED;

    ASSERT(eos_secure_boot_lock_debug() != EOS_OK);
    ASSERT(debug_lock_calls == 1);
}

/* A status query that itself fails cannot confirm the lock either. */
TEST(test_lock_debug_fails_when_the_status_query_fails)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();
    sim_debug_status_rc = EOS_ERR_GENERIC;

    ASSERT(eos_secure_boot_lock_debug() != EOS_OK);
    ASSERT(debug_lock_calls == 1);
}

/* A permanently locked port (RDP level 2 on STM32F4) is locked. */
TEST(test_lock_debug_reports_success_when_status_is_permanent)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();
    sim_reported_debug_status = EOS_DEBUG_STATUS_PERMANENT;

    ASSERT(eos_secure_boot_lock_debug() == EOS_OK);
    ASSERT(debug_lock_calls == 1);
}

/* #184, pinned directly: the old implementation wrote 0xFF to OTP
 * 0x1F0 and called that a lock. Erased OTP already reads 0xFF, so that
 * write changed nothing -- and the mock used to hide it by modelling
 * OTP as zeroed RAM, where 0xFF over 0x00 looked like a real change.
 * The fixture now models erased-as-0xFF, and the lock must not write
 * OTP at all: the byte stays erased and no otp_write happens. */
TEST(test_lock_debug_never_touches_the_otp_lock_byte)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();
    ASSERT(sim_otp[OTP_DEBUG_LOCK_OFFSET] == 0xFF);

    ASSERT(eos_secure_boot_lock_debug() == EOS_OK);
    ASSERT(otp_write_calls == 0);
    ASSERT(sim_otp[OTP_DEBUG_LOCK_OFFSET] == 0xFF);
}

/* The three tests above assert what the *helper* reports. This one asserts
 * what the boot *does* with that report, which is the behaviour this PR
 * actually changed -- the early return at step 7. Without it the suite
 * covers "attempted" while the file's own docblock claims "enforced", and a
 * revert of the step-7 branch would leave every other test passing. */
TEST(test_secure_boot_refuses_when_the_debug_lock_cannot_be_taken)
{
    provide_otp_write = 1;          /* OTP present, but no debug_lock op */
    provide_debug_lock = 0;
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

/* The read-back refusal, at boot level: the lock reports success but
 * the status still reads UNLOCKED, so step 7 refuses the boot rather
 * than recording a locked attestation for an open port. */
TEST(test_secure_boot_refuses_when_the_lock_status_disagrees)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
    reset_fixture();
    stage_bootable_image();
    sim_reported_debug_status = EOS_DEBUG_STATUS_UNLOCKED;

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

/* And with a working lock, lock_debug: true boots and the lock is taken
 * through the debug_lock op -- never through OTP. */
TEST(test_secure_boot_proceeds_when_the_debug_lock_succeeds)
{
    provide_otp_write = 1;
    provide_debug_lock = 1;
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
    ASSERT(debug_lock_calls == 1);
    ASSERT(otp_write_calls == 0);
}

/* Step 4's two new refusals have NO test here, deliberately, and this note is
 * the record of why rather than an omission to be discovered later.
 *
 * Reaching step 4 requires passing step 3, which is a real Ed25519 signature
 * over the header prefix checked against the keystore anchor. A fixture for
 * that is buildable -- the keystore ships RFC 8032 TEST 1's public key and
 * the matching private key is in the RFC -- but the machinery for it belongs
 * to #88 (tools/gen_signed_image_fixture.py), not here. (When this note was
 * first written the shipped key was *not* that key -- it was off the curve
 * from byte 21 on, so no such fixture could have verified; that is fixed in
 * core/keystore.c and pinned by test_keystore.c.)
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

/* ---- Anti-rollback counter placement ----
 *
 * eos_secure_boot_update_rollback() used to write its 4 bytes at OTP 0x180,
 * which is where core/recovery.c reads the recovery shared secret (#159);
 * #160 moved the write to the security_version field at 0x144. That fixed
 * the address but not the encoding: the field is a unary fuse bitmap owned
 * by the board's monotonic counter (boards/stm32f4), and OTP bits only
 * clear, so a 4-byte value write could not raise the counter -- raising it
 * needs 0 -> 1 transitions -- and would land arbitrary bytes in the bitmap
 * instead. update_rollback() now advances the floor through the monotonic
 * counter, one fuse per step, and never writes OTP directly: the field is
 * the board's to burn, and the recovery secret is never touched. The
 * field-isolation half of that promise is pinned by
 * tests/unit/test_stm32f4_otp_counter.c; what is pinned here is the
 * advance itself. */

TEST(test_update_rollback_advances_the_floor_one_fuse_at_a_time)
{
    provide_otp_write = 1;
    provide_monotonic = 1;
    reset_fixture();
    memset(sim_otp, 0xA5, sizeof(sim_otp));

    ASSERT(eos_secure_boot_update_rollback(5) == EOS_OK);
    ASSERT(sim_counter == 5);
    ASSERT(sim_monotonic_increment_calls == 5);

    /* No direct OTP write: the counter field is the board's to burn. */
    ASSERT(otp_write_calls == 0);
    for (uint32_t i = 0; i < OTP_SIZE; i++)
        ASSERT(sim_otp[i] == 0xA5);
}

TEST(test_update_rollback_never_lowers_the_floor)
{
    provide_otp_write = 1;
    provide_monotonic = 1;
    reset_fixture();
    sim_counter = 5;

    ASSERT(eos_secure_boot_update_rollback(3) == EOS_OK);
    ASSERT(sim_counter == 5);
    ASSERT(sim_monotonic_increment_calls == 0);

    ASSERT(eos_secure_boot_update_rollback(5) == EOS_OK);
    ASSERT(sim_counter == 5);
    ASSERT(sim_monotonic_increment_calls == 0);
}

TEST(test_update_rollback_refuses_a_jump_past_max_step)
{
    provide_otp_write = 1;
    provide_monotonic = 1;
    reset_fixture();

    ASSERT(eos_secure_boot_update_rollback(EOS_ROLLBACK_MAX_STEP + 1) ==
           EOS_ERR_INVALID);
    ASSERT(sim_counter == 0);
    ASSERT(sim_monotonic_increment_calls == 0);

    ASSERT(eos_secure_boot_update_rollback(EOS_ROLLBACK_MAX_STEP) == EOS_OK);
    ASSERT(sim_counter == EOS_ROLLBACK_MAX_STEP);
}

TEST(test_update_rollback_fails_closed_without_a_counter)
{
    provide_otp_write = 1;
    provide_monotonic = 0;
    reset_fixture();

    ASSERT(eos_secure_boot_update_rollback(5) == EOS_ERR_NOT_SUPPORTED);
    ASSERT(sim_counter == 0);

    provide_monotonic = 1; /* restore for the suites after this one */
}

TEST(test_update_rollback_reports_a_failed_burn)
{
    provide_otp_write = 1;
    provide_monotonic = 1;
    reset_fixture();
    sim_monotonic_increment_fails = 1;

    ASSERT(eos_secure_boot_update_rollback(3) != EOS_OK);
    ASSERT(sim_counter == 0);
}

int main(void)
{
    printf("=== eBootloader: Secure Boot Debug-Lock Policy Tests ===\n\n");

    run_test_lock_debug_reports_success_when_the_port_is_locked();
    run_test_lock_debug_reports_a_failed_lock();
    run_test_lock_debug_reports_a_board_with_no_debug_lock();
    run_test_lock_debug_fails_when_status_does_not_confirm_the_lock();
    run_test_lock_debug_fails_when_the_status_query_fails();
    run_test_lock_debug_reports_success_when_status_is_permanent();
    run_test_lock_debug_never_touches_the_otp_lock_byte();
    run_test_secure_boot_refuses_when_the_debug_lock_cannot_be_taken();
    run_test_secure_boot_refuses_when_the_lock_status_disagrees();
    run_test_the_same_image_boots_when_no_debug_lock_is_asked_for();
    run_test_secure_boot_proceeds_when_the_debug_lock_succeeds();
    run_test_the_ordinary_boot_path_is_unaffected_by_the_step_4_change();
    run_test_update_rollback_advances_the_floor_one_fuse_at_a_time();
    run_test_update_rollback_never_lowers_the_floor();
    run_test_update_rollback_refuses_a_jump_past_max_step();
    run_test_update_rollback_fails_closed_without_a_counter();
    run_test_update_rollback_reports_a_failed_burn();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
