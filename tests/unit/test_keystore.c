// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_keystore.c
 * @brief Unit tests for boot keystore management
 */

#include "eos_keystore.h"
#include "eos_hal.h"
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

/* A readable revocation store still works normally: slot 0 revoked, slot 1 not. */
TEST(test_revocation_flags_are_honoured)
{
    otp_reset();
    sim_otp[OTP_REVOKE] = 0x01;

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

/* Revoking must not clear another slot's revocation bit, and must report a
 * failure to persist -- an unpersisted revocation is gone at the next reset. */
TEST(test_revocation_is_persisted_without_clobbering_other_slots)
{
    otp_reset();
    sim_otp[OTP_REVOKE] = 0x01;   /* slot 0 already revoked */

    eos_keystore_t ks;
    ASSERT(eos_keystore_init(&ks) == EOS_OK);
    ASSERT(eos_keystore_revoke_slot(&ks, 1) == EOS_OK);

    ASSERT(otp_write_calls == 1);
    ASSERT(sim_otp[OTP_REVOKE] == 0x03);   /* both bits, not just slot 1 */

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

int main(void)
{
    printf("=== eBootloader: Keystore Unit Tests ===\n\n");

    run_test_keystore_init();
    run_test_keystore_get_active_key();
    run_test_keystore_null_args();
    run_test_keystore_security_version();
    run_test_unreadable_revocation_store_does_not_grant_keys();
    run_test_revocation_flags_are_honoured();
    run_test_failed_otp_read_does_not_fall_back_to_the_compiled_key();
    run_test_revocation_is_persisted_without_clobbering_other_slots();
    run_test_revoke_reports_a_failed_persist();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
