// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_stage0_boot_log.c
 * @brief Stage-0 records its boot log entries and hands the head on.
 *
 * ebldr_stage0_main() appended BOOT_START (and, on failure paths,
 * BOOT_FAIL / RECOVERY_ENTER) without ever calling eos_boot_log_init(),
 * so eos_boot_log_append() was a no-op and every stage-0 entry was lost.
 *
 * stage0/ is otherwise only compiled by a cross build; this suite
 * compiles stage0/jump_stage1.c, recovery_entry.c and watchdog.c for the
 * host against a simulated board. The board's jump() and uart_recv()
 * longjmp back here, standing in for the handoff to stage-1 and for the
 * recovery command loop, which never returns.
 */

#include "eos_hal.h"
#include "eos_bootctl.h"
#include "eos_boot_log.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void ebldr_stage0_main(void);

#define SIM_FLASH_SIZE   (64 * 1024)
#define SIM_BOOTCTL      0x1000u
#define SIM_BOOTCTL_BAK  0x2000u
#define SIM_LOG          0x3000u
#define SIM_STAGE1_OFF   0x8000u
#define START_HEAD       5u

#define STAGE1_SIZE      256u

static uint8_t sim_flash[SIM_FLASH_SIZE];

#ifdef EBLDR_VERIFY_STAGE1
/* What tools/embed_stage1_hash.py generates for a real build: the size and
 * SHA-256 of the stage-1 image. The image is fill_stage1()'s pattern, so
 * stage-0's hash check passes and the jump is reached. Recompute with
 *   python3 -c "import hashlib; print(hashlib.sha256(bytes(((i*7+1)&0xFF)
 *               for i in range(256))).hexdigest())"
 */
const uint32_t stage1_expected_size = STAGE1_SIZE;
const uint8_t stage1_expected_hash[32] = {
    0x2d, 0x7f, 0x58, 0x0f, 0xd4, 0x71, 0x06, 0x54,
    0x5e, 0x24, 0xf1, 0x04, 0xf1, 0xee, 0xf1, 0x0e,
    0x75, 0x81, 0x8a, 0x87, 0x47, 0x3f, 0xfe, 0x9a,
    0x43, 0xac, 0x69, 0x8a, 0x39, 0xdd, 0xe7, 0xf2,
};
#endif

static void fill_stage1(void)
{
    uint32_t i;
    for (i = 0; i < STAGE1_SIZE; i++)
        sim_flash[SIM_STAGE1_OFF + i] = (uint8_t)(i * 7u + 1u);
}
static jmp_buf exit_jmp;
static int jumped;
static uint32_t jump_addr;

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

static eos_reset_reason_t sim_reset_reason(void)
{
    return EOS_RESET_PIN;
}

static void sim_jump(uint32_t addr)
{
    jumped = 1;
    jump_addr = addr;
    longjmp(exit_jmp, 1);
}

static int sim_uart_init(uint32_t baud)
{
    (void)baud;
    return EOS_OK;
}

static int sim_uart_send(const void *buf, size_t len)
{
    (void)buf;
    (void)len;
    return EOS_OK;
}

/* The recovery command loop never returns; leave it on its first read. */
static int sim_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    (void)buf;
    (void)len;
    (void)timeout_ms;
    longjmp(exit_jmp, 1);
}

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size          = SIM_FLASH_SIZE,
    .app_vector_offset   = SIM_STAGE1_OFF,
    .bootctl_addr        = SIM_BOOTCTL,
    .bootctl_backup_addr = SIM_BOOTCTL_BAK,
    .log_addr            = SIM_LOG,
    .flash_read          = sim_flash_read,
    .flash_write         = sim_flash_write,
    .flash_erase         = sim_flash_erase,
    .get_reset_reason    = sim_reset_reason,
    .jump                = sim_jump,
    .uart_init           = sim_uart_init,
    .uart_send           = sim_uart_send,
    .uart_recv           = sim_uart_recv,
};

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        memset(sim_flash, 0xFF, sizeof(sim_flash)); \
        fill_stage1(); \
        jumped = 0; \
        jump_addr = 0; \
        eos_hal_init(&sim_ops); \
        printf("  %-58s ", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

static void persist_bootctl(uint32_t flags)
{
    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    bctl.log_head = START_HEAD;
    bctl.flags = flags;
    ASSERT(eos_bootctl_save(&bctl) == EOS_OK);
}

static void run_stage0(void)
{
    if (setjmp(exit_jmp) == 0)
        ebldr_stage0_main();
}

static void assert_entry(uint32_t index, uint32_t event, uint32_t detail)
{
    eos_boot_log_entry_t e;
    ASSERT(eos_boot_log_read(index, &e) == EOS_OK);
    ASSERT(e.event == event);
    ASSERT(e.slot == EOS_SLOT_NONE);
    ASSERT(e.detail == detail);
}

TEST(test_stage0_logs_boot_start_at_the_persisted_head)
{
    persist_bootctl(0);
    run_stage0();

    ASSERT(jumped);
    ASSERT(jump_addr == SIM_STAGE1_OFF);
    assert_entry(START_HEAD, EOS_LOG_BOOT_START, EOS_RESET_PIN);
#ifdef EBLDR_VERIFY_STAGE1
    assert_entry(START_HEAD + 1, EOS_LOG_IMAGE_VALID, 0);
#endif
}

TEST(test_stage0_hands_stage1_a_head_past_its_entries)
{
    eos_bootctl_t bctl;

    persist_bootctl(0);
    run_stage0();

    /* Stage-1 opens the log at the persisted head; it must not be the
     * slot stage-0 just wrote, or stage-1 overwrites it. */
    ASSERT(jumped);
    ASSERT(eos_bootctl_load(&bctl) == EOS_OK);
#ifdef EBLDR_VERIFY_STAGE1
    ASSERT(bctl.log_head == START_HEAD + 2);    /* BOOT_START, IMAGE_VALID */
#else
    ASSERT(bctl.log_head == START_HEAD + 1);    /* BOOT_START */
#endif
}

TEST(test_stage0_recovery_path_keeps_its_entries)
{
    eos_bootctl_t bctl;

    persist_bootctl(EOS_FLAG_FORCE_RECOVERY);
    run_stage0();

    ASSERT(!jumped);
    assert_entry(START_HEAD, EOS_LOG_BOOT_START, EOS_RESET_PIN);
    assert_entry(START_HEAD + 1, EOS_LOG_RECOVERY_ENTER, 0);
    ASSERT(eos_bootctl_load(&bctl) == EOS_OK);
    ASSERT(bctl.log_head == START_HEAD + 2);
}

int main(void)
{
    printf("Stage-0 boot log\n\n");

    run_test_stage0_logs_boot_start_at_the_persisted_head();
    run_test_stage0_hands_stage1_a_head_past_its_entries();
    run_test_stage0_recovery_path_keeps_its_entries();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
