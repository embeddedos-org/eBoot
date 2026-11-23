// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_bootloader_replace.c
 * @brief Privileged in-band stage-1 replacement (ADR-022).
 *
 * The bootloader region is refused to every writer by the HAL allow-list
 * (#191). This suite pins the privileged path through that guard:
 *
 *  - call_request() verifies the staged stage-1 image
 *    (§3: structure, target identity, SHA-256 integrity, Ed25519
 *    signature, anti-rollback counter), writes the PENDING intent record,
 *    and resets -- and refuses a bad image before anything is recorded;
 *  - the stage-0 executor (call_executor()) re-verifies
 *    in stage-0, lifts WRP, copies the staged payload into the stage-1
 *    region through the token-gated privileged HAL entries, verifies the
 *    written bytes, re-applies WRP, clears the record, and resets;
 *  - the privileged entries refuse unprivileged callers exactly like the
 *    allow-list, and the allow-list still refuses the region after the
 *    copy;
 *  - every fail-closed row of the design note §5 table is exercised.
 *
 * The mock board uses STM32F4 geometry (flash base 0x08000000, stage-1
 * at 0x08004000, recovery slot at 0x080A0000) and the board id "stm32f4",
 * so the committed signed fixtures (tests/vectors/
 * bootloader_replace_fixture.h, signed with the RFC 8032 TEST 1 key --
 * the keystore's compiled-in development anchor) verify. The WRP
 * program path itself is pinned by
 * tests/unit/test_stm32f4_bootloader_replace.c against the real STM32F4
 * code; here the protect/unprotect ops are a mock state bit, and the
 * needs_reset property is configurable so both transaction shapes run:
 * immediate-effect boards copy in the same boot, reset-required boards
 * (STM32F4) reset between lifting protection and copying.
 */

#include "eos_bootloader_replace.h"
#include "bootloader_replace.h"
#include "eos_bootctl.h"
#include "eos_bootloader_protect.h"
#include "eos_hal.h"
#include "eos_image.h"
#include "eos_boot_log.h"

#include "../vectors/bootloader_replace_fixture.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Simulated flash + board (STM32F4 geometry) ---- */

#define FLASH_BASE      0x08000000u
#define FLASH_SIZE      (1024u * 1024u)
#define BOOTLOADER_ADDR FLASH_BASE
#define BOOTLOADER_SIZE (64u * 1024u)   /* stage-0 16K + stage-1 48K */
#define STAGE1_ADDR     0x08004000u
#define STAGE1_SIZE     (48u * 1024u)
#define LOG_ADDR        0x08010000u
#define SLOT_A_ADDR     0x08020000u
#define SLOT_A_SIZE     (256u * 1024u)
#define SLOT_B_ADDR     0x08060000u
#define SLOT_B_SIZE     (256u * 1024u)
#define RECOVERY_ADDR   0x080A0000u
#define RECOVERY_SIZE   (128u * 1024u)
#define BOOTCTL_ADDR    0x080C0000u
#define BOOTCTL_BAK     0x080E0000u

static uint8_t sim_flash[FLASH_SIZE];

typedef struct {
    int write_called;
    int erase_called;
    int stage1_write_called;
    int stage1_erase_called;
    int protect_called;
    int unprotect_called;
    int protect_rc;
    int unprotect_rc;
    bool protected;
    bool needs_reset;
    int reset_count;
    uint32_t floor;
    int monotonic_rc;
    int increment_called;
    int fail_write_once;
    int fail_stage1_write_once;
    int fail_erase_once;
    bool has_unprotect;
    bool has_protect;
    bool has_board_id;
    bool has_region;
} mock_ctx_t;

static mock_ctx_t g_ctx;

/* eos_hal_system_reset() halts (while (1)) after the board op returns,
 * because on a device the reset never returns. The mock therefore
 * longjmps out of the reset instead of returning: callers that expect
 * the reset use call_request()/call_executor()/run_stage0(), which arm
 * this buffer first. */
static jmp_buf reset_jmp;

static bool range_hits_stage1(uint32_t addr, size_t len)
{
    uint64_t end = (uint64_t)addr + len;
    return (uint64_t)addr < (uint64_t)(STAGE1_ADDR + STAGE1_SIZE) &&
           end > (uint64_t)STAGE1_ADDR;
}

static int sim_range_ok(uint32_t addr, size_t len)
{
    return addr >= FLASH_BASE &&
           len <= FLASH_SIZE && (uint64_t)addr + len <= (uint64_t)FLASH_BASE + FLASH_SIZE;
}

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr - FLASH_BASE], len);
    return EOS_OK;
}

static int sim_flash_write(uint32_t addr, const void *buf, size_t len)
{
    g_ctx.write_called++;
    if (range_hits_stage1(addr, len)) g_ctx.stage1_write_called++;
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    if (g_ctx.fail_write_once) {
        g_ctx.fail_write_once = 0;
        return EOS_ERR_FLASH;
    }
    /* Fail only a write that lands in the stage-1 region: the copy's
     * payload write. Bootctl saves (which run before the copy) must
     * not consume the injection. */
    if (g_ctx.fail_stage1_write_once && range_hits_stage1(addr, len)) {
        g_ctx.fail_stage1_write_once = 0;
        return EOS_ERR_FLASH;
    }
    memcpy(&sim_flash[addr - FLASH_BASE], buf, len);
    return EOS_OK;
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
    g_ctx.erase_called++;
    if (range_hits_stage1(addr, len)) g_ctx.stage1_erase_called++;
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    if (g_ctx.fail_erase_once) {
        g_ctx.fail_erase_once = 0;
        return EOS_ERR_FLASH;
    }
    memset(&sim_flash[addr - FLASH_BASE], 0xFF, len);
    return EOS_OK;
}

static int sim_protect(void)
{
    g_ctx.protect_called++;
    if (g_ctx.protect_rc != EOS_OK) return g_ctx.protect_rc;
    g_ctx.protected = true;
    return EOS_OK;
}

static int sim_unprotect(void)
{
    g_ctx.unprotect_called++;
    if (g_ctx.unprotect_rc != EOS_OK) return g_ctx.unprotect_rc;
    g_ctx.protected = false;
    return EOS_OK;
}

static int sim_protect_status(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;
    *status = g_ctx.protected ? EOS_BOOTLOADER_PROTECT_PROTECTED
                              : EOS_BOOTLOADER_PROTECT_UNPROTECTED;
    return EOS_OK;
}

static int sim_monotonic_read(uint32_t *value)
{
    if (g_ctx.monotonic_rc != EOS_OK) return g_ctx.monotonic_rc;
    if (!value) return EOS_ERR_INVALID;
    *value = g_ctx.floor;
    return EOS_OK;
}

static int sim_monotonic_increment(void)
{
    if (g_ctx.monotonic_rc != EOS_OK) return g_ctx.monotonic_rc;
    g_ctx.increment_called++;
    g_ctx.floor++;
    return EOS_OK;
}

static uint32_t sim_tick_val = 0;
static uint32_t sim_get_tick(void) { return sim_tick_val++; }
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t x) { (void)x; }
static void sim_watchdog_feed(void) {}
static void sim_system_reset(void)
{
    g_ctx.reset_count++;
    longjmp(reset_jmp, 1);
}
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static bool sim_recovery_pin(void) { return false; }

static jmp_buf exit_jmp;
static int jumped;
static int recovery_entered;
static uint32_t jump_addr;

static void sim_jump(uint32_t addr)
{
    jumped = 1;
    jump_addr = addr;
    longjmp(exit_jmp, 1);
}

static int sim_uart_init(uint32_t baud) { (void)baud; return EOS_OK; }
static int sim_uart_send(const void *buf, size_t len) { (void)buf; (void)len; return EOS_OK; }
static int sim_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    (void)buf; (void)len; (void)timeout_ms;
    recovery_entered = 1;
    longjmp(exit_jmp, 1);
    return EOS_ERR_TIMEOUT;
}

extern void ebldr_stage0_main(void);

#ifdef EBLDR_VERIFY_STAGE1
/* What tools/embed_stage1_hash.py generates for a real build: the size and
 * SHA-256 of the stage-1 image. The image is fill_stage1()'s pattern, so
 * stage-0's hash check passes while the old stage-1 is installed. */
const uint32_t stage1_expected_size = 256;
const uint8_t stage1_expected_hash[32] = {
    0x2d, 0x7f, 0x58, 0x0f, 0xd4, 0x71, 0x06, 0x54,
    0x5e, 0x24, 0xf1, 0x04, 0xf1, 0xee, 0xf1, 0x0e,
    0x75, 0x81, 0x8a, 0x87, 0x47, 0x3f, 0xfe, 0x9a,
    0x43, 0xac, 0x69, 0x8a, 0x39, 0xdd, 0xe7, 0xf2,
};
#endif

static eos_board_ops_t mock_ops = {
    .flash_base      = FLASH_BASE,
    .flash_size      = FLASH_SIZE,
    .slot_a_addr     = SLOT_A_ADDR,
    .slot_a_size     = SLOT_A_SIZE,
    .slot_b_addr     = SLOT_B_ADDR,
    .slot_b_size     = SLOT_B_SIZE,
    .recovery_addr   = RECOVERY_ADDR,
    .recovery_size   = RECOVERY_SIZE,
    .bootctl_addr    = BOOTCTL_ADDR,
    .bootctl_backup_addr = BOOTCTL_BAK,
    .log_addr        = LOG_ADDR,
    .app_vector_offset = 0x4000,
    .bootloader_addr = BOOTLOADER_ADDR,
    .bootloader_size = BOOTLOADER_SIZE,
    .board_id        = "stm32f4",
    .bootloader_protect_needs_reset = false,
    .flash_read      = sim_flash_read,
    .flash_write     = sim_flash_write,
    .flash_erase     = sim_flash_erase,
    .flash_protect_bootloader = sim_protect,
    .flash_protect_status     = sim_protect_status,
    .flash_unprotect_bootloader = sim_unprotect,
    .monotonic_read  = sim_monotonic_read,
    .monotonic_increment = sim_monotonic_increment,
    .watchdog_init   = sim_noop_u32,
    .watchdog_feed   = sim_watchdog_feed,
    .get_reset_reason = sim_reset_reason,
    .system_reset    = sim_system_reset,
    .recovery_pin_asserted = sim_recovery_pin,
    .jump            = sim_jump,
    .uart_init       = sim_uart_init,
    .uart_send       = sim_uart_send,
    .uart_recv       = sim_uart_recv,
    .get_tick_ms     = sim_get_tick,
    .disable_interrupts = sim_noop,
    .enable_interrupts  = sim_noop,
    .deinit_peripherals = sim_noop,
};

/* The ops struct is mutated per test (missing op, missing board id), so
 * the live view is rebuilt here: the executor and the request side read
 * the board through eos_hal_get_ops(). */
static void apply_ops(void)
{
    mock_ops.bootloader_size = g_ctx.has_region ? BOOTLOADER_SIZE : 0;
    mock_ops.board_id = g_ctx.has_board_id ? "stm32f4" : NULL;
    mock_ops.flash_unprotect_bootloader =
        g_ctx.has_unprotect ? sim_unprotect : NULL;
    mock_ops.flash_protect_bootloader =
        g_ctx.has_protect ? sim_protect : NULL;
    mock_ops.bootloader_protect_needs_reset = g_ctx.needs_reset;
    eos_hal_init(&mock_ops);
}

static void setup(void)
{
    memset(sim_flash, 0xFF, sizeof(sim_flash));
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.protect_rc = EOS_OK;
    g_ctx.unprotect_rc = EOS_OK;
    g_ctx.monotonic_rc = EOS_OK;
    g_ctx.has_unprotect = true;
    g_ctx.has_protect = true;
    g_ctx.has_board_id = true;
    g_ctx.has_region = true;
    g_ctx.protected = true;  /* a fielded device boots with WRP applied */
    sim_tick_val = 0;
    jumped = 0;
    recovery_entered = 0;
    jump_addr = 0;
    apply_ops();
    /* The old stage-1: a recognisable pattern, not a valid image. */
    memset(&sim_flash[STAGE1_ADDR - FLASH_BASE], 0xA5, STAGE1_SIZE);
}

/* ---- Test harness ---- */

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        setup(); \
        printf("  %-58s ", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("[OK]\n"); \
    } \
    static void name(void)

/* ---- Fixture helpers ---- */

static void stage_image(const unsigned char *img, size_t len)
{
    memcpy(&sim_flash[RECOVERY_ADDR - FLASH_BASE], img, len);
}

static void stage_good(void)
{
    stage_image(eos_btldr_fixture_image_good, EOS_BTLDR_FIXTURE_GOOD_LEN);
}

static void fill_stage1_pattern(void)
{
    for (uint32_t i = 0; i < 256; i++)
        sim_flash[STAGE1_ADDR - FLASH_BASE + i] = (uint8_t)(i * 7u + 1u);
}

/* Load the bootctl, tolerating the erased-flash case: a test that never
 * saved has both copies corrupt, and load returns EOS_ERR_CRC with
 * defaults. Tests that need a saved record assert its fields next,
 * which is the check that the save landed. */
static void load_bctl(eos_bootctl_t *bctl)
{
    int rc = eos_bootctl_load(bctl);
    ASSERT(rc == EOS_OK || rc == EOS_ERR_CRC);
}

/* Write a PENDING intent directly, without going through request():
 * for executor tests that need a record the request side would refuse. */
static void write_intent_direct(uint16_t version, uint16_t state,
                                uint32_t slot, const uint8_t *digest,
                                uint32_t counter, uint32_t size)
{
    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    bctl.replace_intent.magic = EOS_BTLDR_REPLACE_MAGIC;
    bctl.replace_intent.version = version;
    bctl.replace_intent.state = state;
    bctl.replace_intent.staging_slot = slot;
    if (digest)
        memcpy(bctl.replace_intent.image_sha256, digest, 32);
    bctl.replace_intent.image_counter = counter;
    bctl.replace_intent.image_size = size;
    ASSERT(eos_bootctl_save(&bctl) == EOS_OK);
}

static void write_intent_for_good(uint16_t state)
{
    eos_image_header_t hdr;
    ASSERT(eos_image_parse_header(RECOVERY_ADDR, &hdr) == EOS_OK);
    write_intent_direct(EOS_BTLDR_REPLACE_VERSION, state,
                        EOS_SLOT_RECOVERY, hdr.hash, EOS_BTLDR_FIXTURE_COUNTER,
                        EOS_BTLDR_FIXTURE_GOOD_LEN);
}

/* Run stage-0 and report how it exited: 0 = returned, 1 = jumped to
 * stage-1, 2 = requested a reset, 3 = entered recovery. */
static int run_stage0(void)
{
    if (setjmp(reset_jmp) != 0)
        return 2;
    if (setjmp(exit_jmp) == 0)
        ebldr_stage0_main();
    if (jumped)
        return 1;
    if (recovery_entered)
        return 3;
    return 0;
}

/* Call request() and report EOS_OK when it ends in the reset: the
 * request completed, the reset is how it returns. */
static int call_request(eos_slot_t slot)
{
    if (setjmp(reset_jmp) != 0)
        return EOS_OK;
    return eos_bootloader_replace_request(slot);
}

/* Call the executor and report RESET_PENDING when it ends in the
 * reset. */
static int call_executor(eos_bootctl_t *bctl)
{
    if (setjmp(reset_jmp) != 0)
        return EBLR_REPLACE_RESET_PENDING;
    return eblr_bootloader_replace_execute(bctl);
}

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

/* ================================================================
 * Request side
 * ================================================================ */

TEST(test_request_writes_pending_intent_and_resets)
{
    stage_good();

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    ASSERT(g_ctx.reset_count == 1);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic == EOS_BTLDR_REPLACE_MAGIC);
    ASSERT(bctl.replace_intent.version == EOS_BTLDR_REPLACE_VERSION);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_PENDING);
    ASSERT(bctl.replace_intent.staging_slot == (uint32_t)EOS_SLOT_RECOVERY);
    ASSERT(bctl.replace_intent.image_counter == EOS_BTLDR_FIXTURE_COUNTER);
    ASSERT(bctl.replace_intent.image_size == EOS_BTLDR_FIXTURE_GOOD_LEN);
    eos_image_header_t hdr;
    ASSERT(eos_image_parse_header(RECOVERY_ADDR, &hdr) == EOS_OK);
    ASSERT(memcmp(bctl.replace_intent.image_sha256, hdr.hash, 32) == 0);

    /* The request never touches the bootloader region. */
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(g_ctx.stage1_erase_called == 0);
}

TEST(test_request_refuses_a_non_recovery_staging_slot)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_A) == EOS_ERR_INVALID);
    ASSERT(call_request(EOS_SLOT_B) == EOS_ERR_INVALID);
    ASSERT(call_request(EOS_SLOT_NONE) == EOS_ERR_INVALID);
    ASSERT(g_ctx.reset_count == 0);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_request_refuses_a_second_request_while_pending)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_BUSY);
    ASSERT(g_ctx.reset_count == 1);
}

TEST(test_request_refuses_an_unsigned_image)
{
    uint8_t bad[EOS_BTLDR_FIXTURE_GOOD_LEN];
    memcpy(bad, eos_btldr_fixture_image_good, sizeof(bad));
    bad[100] ^= 0xFF;  /* a signature byte */
    stage_image(bad, sizeof(bad));

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_SIGNATURE);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
    ASSERT(g_ctx.reset_count == 0);
}

TEST(test_request_refuses_a_corrupt_payload_before_signature)
{
    uint8_t bad[EOS_BTLDR_FIXTURE_GOOD_LEN];
    memcpy(bad, eos_btldr_fixture_image_good, sizeof(bad));
    bad[156 + 10] ^= 0xFF;  /* a payload byte */
    stage_image(bad, sizeof(bad));

    /* The payload hash no longer matches: refused at integrity, before
     * any signature work. */
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_CRC);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_request_refuses_a_crc_only_image)
{
    stage_image(eos_btldr_fixture_image_crc_only,
                EOS_BTLDR_FIXTURE_CRC_ONLY_LEN);

    /* Signed, but CRC-only: the replacement path requires SHA-256. */
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_request_refuses_an_image_for_another_board)
{
    stage_image(eos_btldr_fixture_image_wrong_board,
                EOS_BTLDR_FIXTURE_WRONG_BOARD_LEN);

    /* Signed and well-formed, but the TLV target names stm32h7. */
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_request_refuses_an_image_for_the_application_slots)
{
    uint8_t bad[EOS_BTLDR_FIXTURE_GOOD_LEN];
    memcpy(bad, eos_btldr_fixture_image_good, sizeof(bad));
    /* Point the image at slot A, consistently: parse passes, the
     * target check refuses it before any signature work. */
    bad[12] = 0x00; bad[13] = 0x00; bad[14] = 0x02; bad[15] = 0x08;
    bad[16] = 0x00; bad[17] = 0x00; bad[18] = 0x02; bad[19] = 0x08;
    stage_image(bad, sizeof(bad));

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_INVALID);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_request_refuses_a_rollback_image)
{
    g_ctx.floor = EOS_BTLDR_FIXTURE_COUNTER + 4;
    stage_good();

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_ANTI_ROLLBACK);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_request_refuses_without_a_monotonic_counter)
{
    g_ctx.monotonic_rc = EOS_ERR_NOT_SUPPORTED;
    stage_good();

    /* No counter, no proof the image is current: fail closed. (This
     * test runs without EBLDR_ALLOW_NO_ROLLBACK_COUNTER, as CI does.) */
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_NOT_SUPPORTED);
}

TEST(test_request_refuses_without_an_unprotect_primitive)
{
    g_ctx.has_unprotect = false;
    apply_ops();
    stage_good();

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_NOT_SUPPORTED);
}

TEST(test_request_refuses_without_a_board_identity)
{
    g_ctx.has_board_id = false;
    apply_ops();
    stage_good();

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_NOT_SUPPORTED);
}

TEST(test_request_refuses_without_a_bootloader_region)
{
    g_ctx.has_region = false;
    apply_ops();
    stage_good();

    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_NOT_SUPPORTED);
}

TEST(test_request_refuses_an_empty_staging_slot)
{
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_ERR_NO_IMAGE);
}

/* ================================================================
 * Privileged HAL entries: the token gate
 * ================================================================ */

TEST(test_privileged_write_without_a_token_is_refused)
{
    uint8_t buf[16] = {0};
    ASSERT(eos_hal_flash_write_bootloader(STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_erase_bootloader(STAGE1_ADDR, 0x4000) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(g_ctx.stage1_erase_called == 0);

    /* The allow-list refusal and the token refusal are the same code:
     * an unprivileged caller cannot tell which guard stopped it. */
    ASSERT(eos_hal_flash_write(STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
}

TEST(test_privileged_write_with_a_token_is_scoped_to_the_region)
{
    uint8_t buf[16] = {0};
    eos_hal_bootloader_token_mint();

    /* Inside the bootloader region: the write and erase go through. */
    ASSERT(eos_hal_flash_write_bootloader(STAGE1_ADDR, buf, 16) == EOS_OK);
    ASSERT(eos_hal_flash_erase_bootloader(STAGE1_ADDR, 0x4000) == EOS_OK);
    ASSERT(g_ctx.stage1_write_called == 1);
    ASSERT(g_ctx.stage1_erase_called == 1);

    /* Outside it: refused, even with a token. The privileged path
     * writes the bootloader region and nothing else. */
    ASSERT(eos_hal_flash_write_bootloader(SLOT_A_ADDR, buf, 16) == EOS_ERR_INVALID);
    ASSERT(eos_hal_flash_erase_bootloader(SLOT_A_ADDR, 0x4000) == EOS_ERR_INVALID);
    ASSERT(eos_hal_flash_write_bootloader(BOOTLOADER_ADDR - 16, buf, 32) == EOS_ERR_INVALID);
    ASSERT(eos_hal_flash_write_bootloader(BOOTLOADER_ADDR + BOOTLOADER_SIZE - 16, buf, 32) == EOS_ERR_INVALID);

    eos_hal_bootloader_token_clear();
    ASSERT(eos_hal_flash_write_bootloader(STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
}

/* ================================================================
 * Executor: immediate-effect board (one boot)
 * ================================================================ */

TEST(test_executor_copies_in_one_boot_when_protection_is_immediate)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    int resets_before = g_ctx.reset_count;

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_RESET_PENDING);
    ASSERT(g_ctx.reset_count == resets_before + 1);

    /* The staged payload -- not the header -- landed in the stage-1
     * region, WRP was lifted and re-applied around the copy, and the
     * record is cleared. */
    ASSERT(g_ctx.unprotect_called == 1);
    ASSERT(g_ctx.protect_called == 1);
    ASSERT(g_ctx.protected == true);
    ASSERT(g_ctx.stage1_erase_called == 1);
    ASSERT(g_ctx.stage1_write_called == 1);
    ASSERT(memcmp(&sim_flash[STAGE1_ADDR - FLASH_BASE],
                  &eos_btldr_fixture_image_good[156],
                  EOS_BTLDR_FIXTURE_PAYLOAD_LEN) == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);

    /* The token is cleared when the copy ends: the privileged path is
     * closed again, and the allow-list still refuses the region. */
    uint8_t buf[16] = {0};
    ASSERT(eos_hal_flash_write_bootloader(STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
    ASSERT(eos_hal_flash_write(STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
}

TEST(test_executor_skips_the_copy_when_already_installed)
{
    stage_good();
    /* The payload is already installed: a post-copy reset whose clear
     * never landed, or a COPYING save that never landed. */
    memcpy(&sim_flash[STAGE1_ADDR - FLASH_BASE],
           &eos_btldr_fixture_image_good[156],
           EOS_BTLDR_FIXTURE_PAYLOAD_LEN);
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);

    /* No erase, no write, no lift: WRP is re-applied, the record is
     * cleared, and the new stage-1 boots. */
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(g_ctx.unprotect_called == 0);
    ASSERT(g_ctx.protect_called == 1);
    ASSERT(g_ctx.protected == true);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_refuses_a_corrupt_staged_image_and_boots_old_stage1)
{
    uint8_t bad[EOS_BTLDR_FIXTURE_GOOD_LEN];
    memcpy(bad, eos_btldr_fixture_image_good, sizeof(bad));
    bad[156 + 10] ^= 0xFF;
    stage_image(bad, sizeof(bad));
    write_intent_for_good(EOS_BTLDR_REPLACE_PENDING);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);

    /* Nothing was lifted, erased, or written; the record is cleared
     * and the old stage-1 is untouched. */
    ASSERT(g_ctx.unprotect_called == 0);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(g_ctx.reset_count == 0);
    ASSERT(sim_flash[STAGE1_ADDR - FLASH_BASE] == 0xA5);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_refuses_a_record_that_disagrees_with_the_image)
{
    stage_good();
    eos_image_header_t hdr;
    ASSERT(eos_image_parse_header(RECOVERY_ADDR, &hdr) == EOS_OK);
    /* The record's digest is not the staged image's digest. */
    uint8_t wrong_digest[32];
    memset(wrong_digest, 0x11, sizeof(wrong_digest));
    write_intent_direct(EOS_BTLDR_REPLACE_VERSION,
                        EOS_BTLDR_REPLACE_PENDING, EOS_SLOT_RECOVERY,
                        wrong_digest, EOS_BTLDR_FIXTURE_COUNTER,
                        EOS_BTLDR_FIXTURE_GOOD_LEN);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_halts_when_wrp_lift_fails)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    g_ctx.unprotect_rc = EOS_ERR_FLASH;

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EOS_ERR_FLASH);

    /* Nothing was erased or written, and the record stays PENDING:
     * the device halts instead of booting while believing itself
     * protected. */
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_PENDING);
}

TEST(test_executor_halts_when_wrp_reapply_fails)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    g_ctx.protect_rc = EOS_ERR_FLASH;

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EOS_ERR_FLASH);

    /* The copy happened, but the new stage-1 must not boot
     * unprotected: halt, record stays COPYING. */
    ASSERT(g_ctx.stage1_write_called == 1);
    ASSERT(memcmp(&sim_flash[STAGE1_ADDR - FLASH_BASE],
                  &eos_btldr_fixture_image_good[156],
                  EOS_BTLDR_FIXTURE_PAYLOAD_LEN) == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_COPYING);
}

TEST(test_executor_halts_when_the_copy_write_fails)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    g_ctx.fail_stage1_write_once = 1;

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EOS_ERR_FLASH);

    /* The region was erased and the write failed: halt, record stays
     * COPYING, and the privileged path is closed again. */
    ASSERT(g_ctx.stage1_erase_called == 1);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_COPYING);
    uint8_t buf[16] = {0};
    ASSERT(eos_hal_flash_write_bootloader(STAGE1_ADDR, buf, 16) == EOS_ERR_WRITE_PROTECTED);
}

TEST(test_executor_clears_an_unknown_version_and_boots)
{
    stage_good();
    write_intent_for_good(EOS_BTLDR_REPLACE_PENDING);
    eos_bootctl_t bctl;
    load_bctl(&bctl);
    bctl.replace_intent.version = 99;
    ASSERT(eos_bootctl_save(&bctl) == EOS_OK);

    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_clears_an_unknown_state_and_boots)
{
    stage_good();
    write_intent_direct(EOS_BTLDR_REPLACE_VERSION, 7, EOS_SLOT_RECOVERY,
                        NULL, 0, 0);

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);
    ASSERT(g_ctx.stage1_erase_called == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_with_no_intent_is_a_no_op)
{
    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);
    ASSERT(g_ctx.unprotect_called == 0);
    ASSERT(g_ctx.protect_called == 0);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(g_ctx.reset_count == 0);
}

/* ================================================================
 * Executor: reset-required board (STM32F4, two boots)
 * ================================================================ */

TEST(test_executor_two_boots_when_protection_needs_reset)
{
    g_ctx.needs_reset = true;
    apply_ops();

    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    int resets_before = g_ctx.reset_count;

    /* Boot 1: lift WRP, record COPYING, reset. The stage-1 region
     * is untouched: the copy cannot run until the reset reloads the
     * option bytes. */
    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_RESET_PENDING);
    ASSERT(g_ctx.reset_count == resets_before + 1);
    ASSERT(g_ctx.unprotect_called == 1);
    ASSERT(g_ctx.protected == false);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(sim_flash[STAGE1_ADDR - FLASH_BASE] == 0xA5);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_COPYING);

    /* Boot 2: COPYING, protection already lifted. The copy runs in
     * this boot, WRP is re-applied, the record is cleared, reset. */
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_RESET_PENDING);
    ASSERT(g_ctx.reset_count == resets_before + 2);
    ASSERT(g_ctx.unprotect_called == 1);
    ASSERT(g_ctx.protect_called == 1);
    ASSERT(g_ctx.protected == true);
    ASSERT(g_ctx.stage1_erase_called == 1);
    ASSERT(g_ctx.stage1_write_called == 1);
    ASSERT(memcmp(&sim_flash[STAGE1_ADDR - FLASH_BASE],
                  &eos_btldr_fixture_image_good[156],
                  EOS_BTLDR_FIXTURE_PAYLOAD_LEN) == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_copying_state_with_a_corrupt_staged_image_enters_recovery)
{
    stage_good();
    write_intent_for_good(EOS_BTLDR_REPLACE_COPYING);
    /* The staged image is corrupted after the record was written:
     * a power loss mid-copy is the reason the record says COPYING. */
    uint8_t bad[EOS_BTLDR_FIXTURE_GOOD_LEN];
    memcpy(bad, eos_btldr_fixture_image_good, sizeof(bad));
    bad[156 + 10] ^= 0xFF;
    stage_image(bad, sizeof(bad));

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    /* NO_IMAGE: stage-0 enters recovery. The installed stage-1
     * cannot be trusted (it may be half-written), so it is never
     * booted from this state. */
    ASSERT(call_executor(&bctl) == EOS_ERR_NO_IMAGE);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_COPYING);
}

TEST(test_executor_copying_state_completes_after_a_power_loss)
{
    stage_good();
    write_intent_for_good(EOS_BTLDR_REPLACE_COPYING);
    /* Boot after a power loss during the copy: protection was lifted
     * before the loss, so the copy resumes and finishes. */
    g_ctx.protected = false;

    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_RESET_PENDING);
    ASSERT(g_ctx.protect_called == 1);
    ASSERT(g_ctx.protected == true);
    ASSERT(memcmp(&sim_flash[STAGE1_ADDR - FLASH_BASE],
                  &eos_btldr_fixture_image_good[156],
                  EOS_BTLDR_FIXTURE_PAYLOAD_LEN) == 0);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

TEST(test_executor_copying_state_abandons_when_the_board_gates_fail)
{
    stage_good();
    write_intent_for_good(EOS_BTLDR_REPLACE_COPYING);
    g_ctx.has_unprotect = false;
    apply_ops();

    /* The board lost its unprotect primitive between boots: the
     * replacement cannot proceed. The executor abandons it -- clears
     * the record and lets the boot flow continue -- rather than
     * halting in recovery: the installed stage-1 is still gated by
     * stage-0's hash check before any jump, so a partial stage-1
     * cannot boot, and an intact one boots as the old stage-1. */
    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_PROCEED);
    ASSERT(g_ctx.stage1_erase_called == 0);
    ASSERT(g_ctx.stage1_write_called == 0);
    ASSERT(sim_flash[STAGE1_ADDR - FLASH_BASE] == 0xA5);
    load_bctl(&bctl);
    ASSERT(bctl.replace_intent.magic != EOS_BTLDR_REPLACE_MAGIC);
}

/* ================================================================
 * Counter commit (stage-1 boot after a replacement)
 * ================================================================ */

TEST(test_commit_counter_bumps_the_floor_after_a_replacement)
{
    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    eos_bootctl_t bctl;
    load_bctl(&bctl);
    ASSERT(call_executor(&bctl) == EBLR_REPLACE_RESET_PENDING);

    /* The new stage-1 boots: its counter (read from the staged
     * image, verified to match the installed bytes) becomes the
     * floor. */
    ASSERT(eos_bootloader_replace_commit_counter() == EOS_OK);
    ASSERT(g_ctx.floor == EOS_BTLDR_FIXTURE_COUNTER);
    ASSERT(g_ctx.increment_called == 1);

    /* Idempotent: the second boot does not bump again. */
    ASSERT(eos_bootloader_replace_commit_counter() == EOS_OK);
    ASSERT(g_ctx.floor == EOS_BTLDR_FIXTURE_COUNTER);
    ASSERT(g_ctx.increment_called == 1);
}

TEST(test_commit_counter_refuses_an_unrelated_staged_image)
{
    stage_good();
    /* A replacement image is staged but not installed: the installed
     * stage-1 is the old pattern, so the staged counter must not
     * become the floor. */
    ASSERT(eos_bootloader_replace_commit_counter() == EOS_ERR_NO_IMAGE);
    ASSERT(g_ctx.floor == 0);
    ASSERT(g_ctx.increment_called == 0);
}

TEST(test_commit_counter_is_a_no_op_with_an_empty_recovery_slot)
{
    ASSERT(eos_bootloader_replace_commit_counter() == EOS_ERR_NO_IMAGE);
    ASSERT(g_ctx.floor == 0);
}

TEST(test_commit_counter_refuses_a_tampered_staged_image)
{
    uint8_t bad[EOS_BTLDR_FIXTURE_GOOD_LEN];
    memcpy(bad, eos_btldr_fixture_image_good, sizeof(bad));
    bad[100] ^= 0xFF;  /* a signature byte */
    stage_image(bad, sizeof(bad));
    /* The tampered image must not raise the floor, even though the
     * installed stage-1 happens to match its payload. */
    memcpy(&sim_flash[STAGE1_ADDR - FLASH_BASE],
           &eos_btldr_fixture_image_good[156],
           EOS_BTLDR_FIXTURE_PAYLOAD_LEN);

    ASSERT(eos_bootloader_replace_commit_counter() == EOS_ERR_SIGNATURE);
    ASSERT(g_ctx.floor == 0);
}

/* ================================================================
 * bootctl v1 -> v2 migration
 * ================================================================ */

TEST(test_bootctl_v1_copies_migrate_to_v2)
{
    /* A v1 bootctl block (92 bytes, as written before this change):
     * every field is uint32_t, version 1, active slot B, log head 7,
     * boot count 42, the rest zeros, CRC32 over the first 88 bytes. */
    uint8_t block[92];
    memset(block, 0, sizeof(block));
    /* Write a uint32_t little-endian at a byte offset. */
    #define PUT32(off, val) do { \
        block[(off)] = (uint8_t)(val); \
        block[(off) + 1] = (uint8_t)((val) >> 8); \
        block[(off) + 2] = (uint8_t)((val) >> 16); \
        block[(off) + 3] = (uint8_t)((val) >> 24); \
    } while (0)
    PUT32(0, EOS_BOOTCTL_MAGIC);
    PUT32(4, 1);            /* version */
    PUT32(8, 1);            /* active_slot = B */
    PUT32(52, 7);           /* log_head */
    PUT32(56, 42);          /* boot_count */
    #undef PUT32
    uint32_t crc = crc32_calc(block, 88);
    block[88] = (uint8_t)crc;
    block[89] = (uint8_t)(crc >> 8);
    block[90] = (uint8_t)(crc >> 16);
    block[91] = (uint8_t)(crc >> 24);
    sim_flash_write(BOOTCTL_ADDR, block, sizeof(block));
    sim_flash_write(BOOTCTL_BAK, block, sizeof(block));

    eos_bootctl_t bctl;
    ASSERT(eos_bootctl_load(&bctl) == EOS_OK);
    ASSERT(bctl.version == EOS_BOOTCTL_VERSION);
    ASSERT(bctl.active_slot == EOS_SLOT_B);
    ASSERT(bctl.log_head == 7);
    ASSERT(bctl.boot_count == 42);
    ASSERT(bctl.replace_intent.magic == 0);
    ASSERT(bctl.replace_intent.state == EOS_BTLDR_REPLACE_IDLE);
}

/* ================================================================
 * stage0_main wiring: the replacement runs inside the boot flow
 * ================================================================ */

TEST(test_stage0_main_runs_replacement_and_clears_the_record)
{
    g_ctx.needs_reset = true;
    apply_ops();
    fill_stage1_pattern();

    stage_good();
    ASSERT(call_request(EOS_SLOT_RECOVERY) == EOS_OK);
    ASSERT(g_ctx.reset_count == 1);

    /* Boot 1: the executor lifts WRP and resets; stage-1 does not
     * run. */
    ASSERT(run_stage0() == 2);
    ASSERT(!jumped);
    ASSERT(!recovery_entered);
    ASSERT(g_ctx.reset_count == 2);
    ASSERT(g_ctx.protected == false);

    /* Boot 2: the copy runs, WRP is re-applied, the record clears,
     * and stage-0 resets to boot the new stage-1. */
    ASSERT(run_stage0() == 2);
    ASSERT(!jumped);
    ASSERT(!recovery_entered);
    ASSERT(g_ctx.reset_count == 3);
    ASSERT(g_ctx.protected == true);
    ASSERT(memcmp(&sim_flash[STAGE1_ADDR - FLASH_BASE],
                  &eos_btldr_fixture_image_good[156],
                  EOS_BTLDR_FIXTURE_PAYLOAD_LEN) == 0);

    /* Boot 3: the new stage-1 is installed, but this stage-0 still
     * verifies it against the build-time hash of the OLD stage-1 --
     * the design note's §3 prerequisite (signature-based stage-1
     * verification) is not yet in the tree. The hash check fails and
     * stage-0 enters recovery, exactly as a hash-pinned stage-0 must. */
    ASSERT(run_stage0() == 3);
    ASSERT(!jumped);
    ASSERT(recovery_entered);
}

int main(void)
{
    printf("Bootloader replacement (ADR-022)\n\n");

    run_test_request_writes_pending_intent_and_resets();
    run_test_request_refuses_a_non_recovery_staging_slot();
    run_test_request_refuses_a_second_request_while_pending();
    run_test_request_refuses_an_unsigned_image();
    run_test_request_refuses_a_corrupt_payload_before_signature();
    run_test_request_refuses_a_crc_only_image();
    run_test_request_refuses_an_image_for_another_board();
    run_test_request_refuses_an_image_for_the_application_slots();
    run_test_request_refuses_a_rollback_image();
    run_test_request_refuses_without_a_monotonic_counter();
    run_test_request_refuses_without_an_unprotect_primitive();
    run_test_request_refuses_without_a_board_identity();
    run_test_request_refuses_without_a_bootloader_region();
    run_test_request_refuses_an_empty_staging_slot();
    run_test_privileged_write_without_a_token_is_refused();
    run_test_privileged_write_with_a_token_is_scoped_to_the_region();
    run_test_executor_copies_in_one_boot_when_protection_is_immediate();
    run_test_executor_skips_the_copy_when_already_installed();
    run_test_executor_refuses_a_corrupt_staged_image_and_boots_old_stage1();
    run_test_executor_refuses_a_record_that_disagrees_with_the_image();
    run_test_executor_halts_when_wrp_lift_fails();
    run_test_executor_halts_when_wrp_reapply_fails();
    run_test_executor_halts_when_the_copy_write_fails();
    run_test_executor_clears_an_unknown_version_and_boots();
    run_test_executor_clears_an_unknown_state_and_boots();
    run_test_executor_with_no_intent_is_a_no_op();
    run_test_executor_two_boots_when_protection_needs_reset();
    run_test_executor_copying_state_with_a_corrupt_staged_image_enters_recovery();
    run_test_executor_copying_state_completes_after_a_power_loss();
    run_test_executor_copying_state_abandons_when_the_board_gates_fail();
    run_test_commit_counter_bumps_the_floor_after_a_replacement();
    run_test_commit_counter_refuses_an_unrelated_staged_image();
    run_test_commit_counter_is_a_no_op_with_an_empty_recovery_slot();
    run_test_commit_counter_refuses_a_tampered_staged_image();
    run_test_bootctl_v1_copies_migrate_to_v2();
    run_test_stage0_main_runs_replacement_and_clears_the_record();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
