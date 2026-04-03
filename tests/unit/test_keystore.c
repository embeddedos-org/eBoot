// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_keystore.c
 * @brief Unit tests for boot keystore management
 */

#include "eos_keystore.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
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

int main(void)
{
    printf("=== eBootloader: Keystore Unit Tests ===\n\n");

    run_test_keystore_init();
    run_test_keystore_get_active_key();
    run_test_keystore_null_args();
    run_test_keystore_security_version();

    tests_run = 4;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
