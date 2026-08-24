// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_storage.c
 * @brief Regression and unit tests for unified storage bounds checking
 *
 * Regression tests prove:
 *  - eos_storage_erase() had no bounds check (out-of-bounds erases succeeded)
 *  - eos_storage_read/write/erase overflow bypass via uint32 wrap-around
 *
 * Functional tests verify normal operations remain correct after fix.
 */

#include "eos_storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/* ---- Mock storage backend ---- */

#define MOCK_TOTAL_SIZE    (64u * 1024u)   /* 64 KB */
#define MOCK_SECTOR_SIZE   4096u
#define MOCK_PAGE_SIZE     256u

typedef struct {
    uint8_t memory[MOCK_TOTAL_SIZE];
    int read_called;
    int write_called;
    int erase_called;
} mock_ctx_t;

static mock_ctx_t g_ctx;

static int mock_init(void *ctx)     { (void)ctx; return 0; }
static uint32_t mock_size(void *ctx)       { (void)ctx; return MOCK_TOTAL_SIZE; }
static uint32_t mock_erase_size(void *ctx) { (void)ctx; return MOCK_SECTOR_SIZE; }

static int mock_read(void *ctx, uint32_t off, void *buf, uint32_t len)
{
    mock_ctx_t *m = (mock_ctx_t *)ctx;
    m->read_called++;
    if (off + len <= MOCK_TOTAL_SIZE && buf)
        memcpy(buf, &m->memory[off], len);
    return 0;
}

static int mock_write(void *ctx, uint32_t off, const void *buf, uint32_t len)
{
    mock_ctx_t *m = (mock_ctx_t *)ctx;
    m->write_called++;
    if (off + len <= MOCK_TOTAL_SIZE && buf)
        memcpy(&m->memory[off], buf, len);
    return 0;
}

static int mock_erase(void *ctx, uint32_t off, uint32_t len)
{
    mock_ctx_t *m = (mock_ctx_t *)ctx;
    m->erase_called++;
    if (off + len <= MOCK_TOTAL_SIZE)
        memset(&m->memory[off], 0xFF, len);
    return 0;
}

static const eos_storage_ops_t mock_ops = {
    .init = mock_init, .read = mock_read, .write = mock_write,
    .erase = mock_erase, .get_size = mock_size, .get_erase_size = mock_erase_size,
};

static void setup(eos_storage_dev_t *dev)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(dev, 0, sizeof(*dev));
    dev->name = "mock";
    dev->type = EOS_STORAGE_SPI_NOR;
    dev->ops  = &mock_ops;
    dev->ctx  = &g_ctx;
    ASSERT(eos_storage_init(dev) == 0);
    ASSERT(dev->total_size == MOCK_TOTAL_SIZE);
}

/* ================================================================
 * REGRESSION: eos_storage_erase — missing bounds check
 * Original code forwarded any (off, len) to driver without validation.
 * ================================================================ */

TEST(test_erase_out_of_bounds)
{
    eos_storage_dev_t dev;
    setup(&dev);

    /* 60 KB offset + 10 KB length = 70 KB > 64 KB capacity */
    ASSERT(eos_storage_erase(&dev, 60u * 1024u, 10u * 1024u) == -1);
    ASSERT(g_ctx.erase_called == 0);

    /* offset entirely past device end */
    ASSERT(eos_storage_erase(&dev, MOCK_TOTAL_SIZE + 4096u, 4096u) == -1);
    ASSERT(g_ctx.erase_called == 0);
}

/* ================================================================
 * REGRESSION: integer overflow bypass via uint32 wrap-around
 * off + len wraps to small value, passing naive > total_size check.
 * ================================================================ */

TEST(test_erase_integer_overflow)
{
    eos_storage_dev_t dev;
    setup(&dev);

    /* 0xFFFFFFFF + 10 wraps to 9, which < 64KB → old code succeeded */
    ASSERT(eos_storage_erase(&dev, 0xFFFFFFFFu, 10u) == -1);
    ASSERT(g_ctx.erase_called == 0);
}

TEST(test_read_integer_overflow)
{
    eos_storage_dev_t dev;
    setup(&dev);
    uint8_t buf[16];

    ASSERT(eos_storage_read(&dev, 0xFFFFFFFFu, buf, 10u) == -1);
    ASSERT(g_ctx.read_called == 0);
}

TEST(test_write_integer_overflow)
{
    eos_storage_dev_t dev;
    setup(&dev);
    uint8_t buf[16] = {0};

    ASSERT(eos_storage_write(&dev, 0xFFFFFFFFu, buf, 10u) == -1);
    ASSERT(g_ctx.write_called == 0);
}

/* ================================================================
 * FUNCTIONAL: normal operations still work correctly after fix
 * ================================================================ */

TEST(test_valid_read_write_erase)
{
    eos_storage_dev_t dev;
    setup(&dev);

    uint8_t wr[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t rd[16] = {0};

    /* in-bounds write and readback */
    ASSERT(eos_storage_write(&dev, 0x1000, wr, 16) == 0);
    ASSERT(eos_storage_read(&dev, 0x1000, rd, 16) == 0);
    ASSERT(memcmp(wr, rd, 16) == 0);

    /* in-bounds erase */
    ASSERT(eos_storage_erase(&dev, 0x1000, MOCK_SECTOR_SIZE) == 0);

    /* exact boundary: last 16 bytes of device */
    ASSERT(eos_storage_write(&dev, MOCK_TOTAL_SIZE - 16, wr, 16) == 0);
    ASSERT(eos_storage_read(&dev, MOCK_TOTAL_SIZE - 16, rd, 16) == 0);
    ASSERT(memcmp(wr, rd, 16) == 0);

    /* exact boundary: last sector */
    ASSERT(eos_storage_erase(&dev, MOCK_TOTAL_SIZE - MOCK_SECTOR_SIZE,
                             MOCK_SECTOR_SIZE) == 0);
}

TEST(test_write_protect_blocks_write_and_erase)
{
    eos_storage_dev_t dev;
    setup(&dev);
    dev.write_protect = true;

    uint8_t buf[16] = {0xAA};
    ASSERT(eos_storage_write(&dev, 0, buf, 16) == -1);
    ASSERT(eos_storage_erase(&dev, 0, MOCK_SECTOR_SIZE) == -1);
    /* read still allowed */
    ASSERT(eos_storage_read(&dev, 0, buf, 16) == 0);
}

int main(void)
{
    printf("=== eBoot Storage Bounds & Overflow Regression Tests ===\n");

    run_test_erase_out_of_bounds();
    run_test_erase_integer_overflow();
    run_test_read_integer_overflow();
    run_test_write_integer_overflow();
    run_test_valid_read_write_erase();
    run_test_write_protect_blocks_write_and_erase();

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
