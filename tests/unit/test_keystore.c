// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_keystore.c
 * @brief Unit tests for boot keystore management
 */

#include "eos_keystore.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include "../vectors/fw_update_test_sigs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Simulated OTP ----
 * The tests above run with no board registered, so eos_hal_otp_read() reports
 * EOS_ERR_NOT_SUPPORTED and the keystore uses the compiled-in key. The tests
 * below register a board so the OTP paths are exercised. */

#define OTP_SIZE 0x200
#define OTP_KEY0 0x100
#define OTP_KEY1 0x120
#define OTP_REVOKE 0x140

static uint8_t sim_otp[OTP_SIZE];
static int otp_read_rc;          /* forced result for reads of any offset */
static int otp_revoke_read_rc;   /* forced result for the revocation offset */
static int otp_write_rc;
static int otp_write_calls;

static int sim_otp_read(uint32_t offset, void *buf, size_t len)
{
    if (offset == OTP_REVOKE && otp_revoke_read_rc != EOS_OK)
        return otp_revoke_read_rc;
    if (otp_read_rc != EOS_OK)
        return otp_read_rc;
    if ((uint64_t)offset + len > OTP_SIZE)
        return EOS_ERR_INVALID;
    memcpy(buf, sim_otp + offset, len);
    return EOS_OK;
}

static int sim_otp_write(uint32_t offset, const void *buf, size_t len)
{
    otp_write_calls++;
    if (otp_write_rc != EOS_OK)
        return otp_write_rc;
    if ((uint64_t)offset + len > OTP_SIZE)
        return EOS_ERR_INVALID;
    memcpy(sim_otp + offset, buf, len);
    return EOS_OK;
}

static const eos_board_ops_t sim_board = {
    .otp_read = sim_otp_read,
    .otp_write = sim_otp_write,
};

/* Provision both OTP key slots with distinguishable non-zero keys. */
static void otp_reset(void)
{
    memset(sim_otp, 0, sizeof(sim_otp));
    memset(sim_otp + OTP_KEY0, 0xA1, 32);
    memset(sim_otp + OTP_KEY1, 0xB2, 32);
    /* Clear-to-revoke (#183): the erased byte (0xFF) is the unrevoked
     * state. The memset above zeroed the revoke byte, which now reads as
     * both slots revoked, so provision it explicitly. */
    sim_otp[OTP_REVOKE] = 0xFF;
    otp_read_rc = EOS_OK;
    otp_revoke_read_rc = EOS_OK;
    otp_write_rc = EOS_OK;
    otp_write_calls = 0;
    eos_hal_init(&sim_board);
}

static void otp_detach(void)
{
    eos_hal_init(NULL);
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
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

TEST(test_keystore_init)
{
    eos_keystore_t ks;
    int rc = eos_keystore_init(&ks);
    ASSERT(rc == EOS_OK);

    /* After init the keystore must report at least one valid key */
    uint32_t count = 0;
    rc = eos_keystore_key_count(&ks, &count);
    ASSERT(rc == EOS_OK);
    ASSERT(count >= 1);
}

TEST(test_keystore_get_active_key)
{
    eos_keystore_t ks;
    int rc = eos_keystore_init(&ks);
    ASSERT(rc == EOS_OK);

    const uint8_t *key = NULL;
    size_t key_len = 0;
    rc = eos_keystore_get_active_key(&ks, &key, &key_len);
    ASSERT(rc == EOS_OK);
    ASSERT(key != NULL);
    ASSERT(key_len == 32); /* Ed25519 public key is 32 bytes */

    /* Key must not be all zeros */
    uint8_t zero[32];
    memset(zero, 0, sizeof(zero));
    ASSERT(memcmp(key, zero, 32) != 0);
}

TEST(test_keystore_null_args)
{
    eos_keystore_t ks;

    ASSERT(eos_keystore_init(NULL) != EOS_OK);
    ASSERT(eos_keystore_get_active_key(NULL, NULL, NULL) != EOS_OK);

    int rc = eos_keystore_init(&ks);
    ASSERT(rc == EOS_OK);

    ASSERT(eos_keystore_get_active_key(&ks, NULL, NULL) != EOS_OK);

    const uint8_t *key = NULL;
    ASSERT(eos_keystore_get_active_key(&ks, &key, NULL) != EOS_OK);
}

TEST(test_keystore_security_version)
{
    eos_keystore_t ks;
    int rc = eos_keystore_init(&ks);
    ASSERT(rc == EOS_OK);

    /* Get the current anti-rollback security version */
    uint32_t version = 0;
    rc = eos_keystore_get_security_version(&ks, &version);
    ASSERT(rc == EOS_OK);

    /* Version must be a sane value (non-zero after init) */
    ASSERT(version >= 1);

    /* Attempting to set a version lower than current must fail */
    uint32_t old_version = version - 1;
    rc = eos_keystore_set_security_version(&ks, old_version);
    ASSERT(rc != EOS_OK);

    /* Setting the same or higher version must succeed */
    rc = eos_keystore_set_security_version(&ks, version);
    ASSERT(rc == EOS_OK);

    rc = eos_keystore_set_security_version(&ks, version + 1);
    ASSERT(rc == EOS_OK);
}


/* A device with OTP keys whose revocation store cannot be read must not keep
 * using those keys. Revocation exists to retire a key believed compromised, so
 * "I could not check" has to mean "do not use", not "not revoked". */
TEST(test_unreadable_revocation_store_does_not_grant_keys)
{
    otp_reset();
    otp_revoke_read_rc = EOS_ERR_FLASH;

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);

    const uint8_t *key = NULL;
    size_t key_len = 0;
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) != EOS_OK);

    uint32_t count = 1;
    ASSERT(eos_keystore_key_count(&ks, &count) == EOS_OK);
    ASSERT(count == 0);

    otp_detach();
}

/* A readable revocation store still works normally: slot 0 revoked, slot 1 not.
 * Clear-to-revoke (#183): bit 0 cleared (burned) revokes slot 0. */
TEST(test_revocation_flags_are_honoured)
{
    otp_reset();
    sim_otp[OTP_REVOKE] = 0xFE;

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);

    const uint8_t *key = NULL;
    size_t key_len = 0;
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) == EOS_OK);

    /* Slot 0 is revoked, so the active key must be slot 1's. */
    uint8_t expect[32];
    memset(expect, 0xB2, sizeof(expect));
    ASSERT(memcmp(key, expect, 32) == 0);

    otp_detach();
}

/* An erased revoke byte revokes nothing (#183): a freshly provisioned
 * device -- revoke byte never written, still 0xFF -- has slot 0 active.
 * Under the old set-to-revoke reading 0xFF meant both slots revoked. */
TEST(test_erased_revoke_byte_revokes_nothing)
{
    otp_reset();
    ASSERT(sim_otp[OTP_REVOKE] == 0xFF);

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);

    const uint8_t *key = NULL;
    size_t key_len = 0;
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) == EOS_OK);

    /* Slot 0 is active, so the key must be slot 0's. */
    uint8_t expect[32];
    memset(expect, 0xA1, sizeof(expect));
    ASSERT(memcmp(key, expect, 32) == 0);

    uint32_t count = 0;
    ASSERT(eos_keystore_key_count(&ks, &count) == EOS_OK);
    ASSERT(count == 2);

    otp_detach();
}

/* An erased key slot is not a provisioned key (#183 second look): a
 * single-key device leaves slot 1 never written, still 0xFF, and that
 * must not count as a second key -- and revoking slot 0 must then leave
 * no active key, not promote the erased slot to active. Under the old
 * init check (valid iff non-zero) slot 1 read as provisioned, key_count
 * reported 2, and after revoking slot 0 get_active_key() handed back
 * 0xFF bytes. */
TEST(test_erased_key_slot_stays_unprovisioned)
{
    otp_reset();
    memset(sim_otp + OTP_KEY1, 0xFF, 32);   /* slot 1 never written */

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);

    uint32_t count = 0;
    ASSERT(eos_keystore_key_count(&ks, &count) == EOS_OK);
    ASSERT(count == 1);   /* slot 0 only */

    /* Slot 0 is active, so the key must be slot 0's. */
    const uint8_t *key = NULL;
    size_t key_len = 0;
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) == EOS_OK);
    uint8_t expect[32];
    memset(expect, 0xA1, sizeof(expect));
    ASSERT(memcmp(key, expect, 32) == 0);

    /* Revoking the only provisioned key leaves no active key. */
    ASSERT(eos_keystore_revoke_slot(&ks, 0) == EOS_OK);
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) != EOS_OK);
    ASSERT(eos_keystore_key_count(&ks, &count) == EOS_OK);
    ASSERT(count == 0);

    otp_detach();
}

/* A board that HAS an OTP but fails to read it has an unknown trust anchor.
 * Falling back to the compiled-in key would let a fault on the OTP bus swap
 * which key the device trusts. */
TEST(test_failed_otp_read_does_not_fall_back_to_the_compiled_key)
{
    otp_reset();
    otp_read_rc = EOS_ERR_FLASH;

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) != EOS_OK);

    otp_detach();
}

/* Revoking must not disturb another slot's revocation bit, and must report a
 * failure to persist -- an unpersisted revocation is gone at the next reset.
 * Clear-to-revoke (#183): revoking clears the slot's bit, so slot 0 already
 * revoked is 0xFE and revoking slot 1 must leave 0xFC. */
TEST(test_revocation_is_persisted_without_clobbering_other_slots)
{
    otp_reset();
    sim_otp[OTP_REVOKE] = 0xFE;   /* slot 0 already revoked */

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_revoke_slot(&ks, 1) == EOS_OK);

    ASSERT(otp_write_calls == 1);
    ASSERT(sim_otp[OTP_REVOKE] == 0xFC);   /* both bits cleared, not just slot 1 */

    otp_detach();
}

TEST(test_revoke_reports_a_failed_persist)
{
    otp_reset();
    otp_write_rc = EOS_ERR_FLASH;

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_revoke_slot(&ks, 0) != EOS_OK);

    /* The revocation still applies for this boot even though it did not stick. */
    uint32_t count = 99;
    ASSERT(eos_keystore_key_count(&ks, &count) == EOS_OK);
    ASSERT(count == 1);   /* slot 1 remains */

    otp_detach();
}

/* The compiled-in anchor claims, in a #warning and in comments, to be the
 * RFC 8032 section 7.1 TEST 1 public key. That claim is what makes it
 * usable for development at all: the matching secret is printed in the RFC,
 * so anyone can sign a test image for a board that falls back to it.
 *
 * From v0.1.0 the array agreed with the RFC for 21 bytes and then diverged,
 * and the bytes it held did not decode to a point on the curve. Every
 * signature check against the fallback failed, and after #104 made signature
 * verification unconditional at install, firmware update refused every
 * image on every board without OTP. Nothing noticed because no test ever
 * asked the fallback key to verify anything.
 *
 * This asks. The vector is RFC 8032 TEST 1 itself: empty message, and the
 * signature the RFC prints for it. On the old bytes eos_ed25519_verify()
 * returns EOS_ERR_SIGNATURE; the two negative checks after it show the
 * accept is discriminating, not a verifier that says yes to everything. */
TEST(test_compiled_in_anchor_verifies_its_own_rfc_vector)
{
    static const uint8_t rfc8032_test1_sig[64] = {
        0xe5,0x56,0x43,0x00,0xc3,0x60,0xac,0x72,0x90,0x86,0xe2,0xcc,0x80,0x6e,0x82,0x8a,
        0x84,0x87,0x7f,0x1e,0xb8,0xe5,0xd9,0x74,0xd8,0x73,0xe0,0x65,0x22,0x49,0x01,0x55,
        0x5f,0xb8,0x82,0x15,0x90,0xa3,0x3b,0xac,0xc6,0x1e,0x39,0x70,0x1c,0xf9,0xb4,0x6b,
        0xd2,0x5b,0xf5,0xf0,0x59,0x5b,0xbe,0x24,0x65,0x51,0x41,0x43,0x8e,0x7a,0x10,0x0b,
    };
    const uint8_t *key = NULL;
    size_t key_len = 0;
    eos_keystore_t ks;

    otp_detach();               /* no OTP at all: the compiled-in path */
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_get_active_key(&ks, &key, &key_len) == EOS_OK);
    ASSERT(key_len == 32);

    /* The bytes are the RFC's bytes, and they verify the RFC's signature.
     *
     * The expected key is eos_test_sig_pubkey from the generated fixture
     * header, not a third hand-typed copy. That public key is derived from
     * the RFC 8032 secret by tools/gen_fw_update_test_sigs.py and pinned to
     * that script by tests/unit/test_fw_update_test_sigs.py, so this compares
     * the compiled-in anchor against a second, independently derived copy of
     * the same key. */
    ASSERT(memcmp(key, eos_test_sig_pubkey, 32) == 0);
    ASSERT(eos_ed25519_verify(rfc8032_test1_sig, key, NULL, 0) == EOS_OK);

    /* Discrimination: the same signature must not verify a different message,
     * and a bit-flipped signature must not verify the empty one. */
    const uint8_t other[1] = { 'x' };
    ASSERT(eos_ed25519_verify(rfc8032_test1_sig, key, other, 1) != EOS_OK);
    uint8_t flipped[64];
    memcpy(flipped, rfc8032_test1_sig, 64);
    flipped[0] ^= 0x01;
    ASSERT(eos_ed25519_verify(flipped, key, NULL, 0) != EOS_OK);
}

int main(void)
{
    printf("=== eBootloader: Keystore Unit Tests ===\n\n");

    run_test_keystore_init();
    run_test_keystore_get_active_key();
    run_test_keystore_null_args();
    run_test_keystore_security_version();
    run_test_unreadable_revocation_store_does_not_grant_keys();
    run_test_revocation_flags_are_honoured();
    run_test_erased_revoke_byte_revokes_nothing();
    run_test_erased_key_slot_stays_unprovisioned();
    run_test_failed_otp_read_does_not_fall_back_to_the_compiled_key();
    run_test_revocation_is_persisted_without_clobbering_other_slots();
    run_test_revoke_reports_a_failed_persist();
    run_test_compiled_in_anchor_verifies_its_own_rfc_vector();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
