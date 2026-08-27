// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_rollback.c
 * @brief Unit tests for the anti-rollback security counter.
 *
 * The mock board models OTP semantics: the counter only ever moves up.
 */

#include "eos_rollback.h"
#include "eos_hal.h"
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

/* ---- Mock OTP-backed monotonic counter ---- */

static uint32_t g_counter;
static int      g_increment_calls;
static int      g_increment_fails;   /* if set, increment reports failure */
static int      g_increment_silent;  /* if set, increment "succeeds" without
                                        advancing — a failed fuse burn */

static int mock_monotonic_read(uint32_t *value)
{
    if (!value) return EOS_ERR_INVALID;
    *value = g_counter;
    return EOS_OK;
}

static int mock_monotonic_increment(void)
{
    g_increment_calls++;
    if (g_increment_fails) return EOS_ERR_GENERIC;
    if (!g_increment_silent) g_counter++;
    return EOS_OK;
}

static const eos_board_ops_t g_ops_with_counter = {
    .monotonic_read = mock_monotonic_read,
    .monotonic_increment = mock_monotonic_increment,
};

static const eos_board_ops_t g_ops_no_counter = { 0 };

static void reset_counter_board(uint32_t initial)
{
    g_counter = initial;
    g_increment_calls = 0;
    g_increment_fails = 0;
    g_increment_silent = 0;
    eos_hal_init(&g_ops_with_counter);
    eos_rollback_clear_staged();
}

/* ---- Verification ---- */

TEST(test_verify_accepts_equal_counter)
{
    reset_counter_board(5);
    ASSERT(eos_rollback_verify(5) == EOS_OK);
}

TEST(test_verify_accepts_newer_counter)
{
    reset_counter_board(5);
    ASSERT(eos_rollback_verify(9) == EOS_OK);
}

TEST(test_verify_rejects_downgrade)
{
    reset_counter_board(5);
    ASSERT(eos_rollback_verify(4) == EOS_ERR_ANTI_ROLLBACK);
    ASSERT(eos_rollback_verify(0) == EOS_ERR_ANTI_ROLLBACK);
}

TEST(test_verify_permissive_without_hardware)
{
    eos_hal_init(&g_ops_no_counter);
    ASSERT(eos_rollback_verify(0) == EOS_OK);
    ASSERT(eos_rollback_get_device_counter(&(uint32_t){0}) == EOS_ERR_NOT_SUPPORTED);
}

/* ---- Commit ---- */

TEST(test_commit_raises_floor)
{
    reset_counter_board(3);
    eos_rollback_stage(6);
    ASSERT(eos_rollback_commit() == EOS_OK);

    uint32_t now = 0;
    ASSERT(eos_rollback_get_device_counter(&now) == EOS_OK);
    ASSERT(now == 6);
    ASSERT(g_increment_calls == 3);

    ASSERT(eos_rollback_verify(5) == EOS_ERR_ANTI_ROLLBACK);
}

TEST(test_commit_without_stage_is_noop)
{
    reset_counter_board(3);
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(g_counter == 3);
    ASSERT(g_increment_calls == 0);
}

TEST(test_commit_is_idempotent)
{
    reset_counter_board(2);
    eos_rollback_stage(4);
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(g_counter == 4);

    int calls_after_first = g_increment_calls;

    eos_rollback_stage(4);
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(g_counter == 4);
    ASSERT(g_increment_calls == calls_after_first);
}

TEST(test_commit_never_lowers_floor)
{
    reset_counter_board(8);
    eos_rollback_stage(3);
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(g_counter == 8);
    ASSERT(g_increment_calls == 0);
}

TEST(test_commit_refuses_implausible_jump)
{
    reset_counter_board(0);
    eos_rollback_stage(EOS_ROLLBACK_MAX_STEP + 1);
    ASSERT(eos_rollback_commit() == EOS_ERR_INVALID);
    ASSERT(g_counter == 0);
    ASSERT(g_increment_calls == 0);

    reset_counter_board(0);
    eos_rollback_stage(EOS_ROLLBACK_MAX_STEP);
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(g_counter == EOS_ROLLBACK_MAX_STEP);
}

TEST(test_commit_reports_hardware_failure)
{
    reset_counter_board(1);
    g_increment_fails = 1;
    eos_rollback_stage(3);
    ASSERT(eos_rollback_commit() != EOS_OK);
}

TEST(test_commit_detects_silent_fuse_failure)
{
    reset_counter_board(1);
    g_increment_silent = 1;
    eos_rollback_stage(3);
    ASSERT(eos_rollback_commit() == EOS_ERR_GENERIC);
    ASSERT(g_counter == 1);
}

TEST(test_commit_unsupported_without_hardware)
{
    eos_hal_init(&g_ops_no_counter);
    eos_rollback_clear_staged();
    eos_rollback_stage(4);
    ASSERT(eos_rollback_commit() == EOS_ERR_NOT_SUPPORTED);
}

/* ---- Staging lifecycle ---- */

TEST(test_clear_staged_prevents_commit)
{
    reset_counter_board(1);
    eos_rollback_stage(5);
    eos_rollback_clear_staged();
    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(g_counter == 1);
    ASSERT(g_increment_calls == 0);
}

TEST(test_downgrade_blocked_after_confirmed_update)
{
    reset_counter_board(1);

    ASSERT(eos_rollback_verify(1) == EOS_OK);

    ASSERT(eos_rollback_verify(2) == EOS_OK);
    eos_rollback_stage(2);
    ASSERT(eos_rollback_commit() == EOS_OK);

    ASSERT(eos_rollback_verify(1) == EOS_ERR_ANTI_ROLLBACK);
    ASSERT(eos_rollback_verify(2) == EOS_OK);
}

int main(void)
{
    printf("=== eBootloader: Anti-Rollback Counter Tests ===\n\n");
    run_test_verify_accepts_equal_counter();
    run_test_verify_accepts_newer_counter();
    run_test_verify_rejects_downgrade();
    run_test_verify_permissive_without_hardware();
    run_test_commit_raises_floor();
    run_test_commit_without_stage_is_noop();
    run_test_commit_is_idempotent();
    run_test_commit_never_lowers_floor();
    run_test_commit_refuses_implausible_jump();
    run_test_commit_reports_hardware_failure();
    run_test_commit_detects_silent_fuse_failure();
    run_test_commit_unsupported_without_hardware();
    run_test_clear_staged_prevents_commit();
    run_test_downgrade_blocked_after_confirmed_update();
    tests_run = 14;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
