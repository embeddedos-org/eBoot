// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_recovery.c
 * @brief Unit tests for the recovery-mode UART protocol handler
 *
 * eos_recovery_enter() runs an infinite command loop, so the simulated
 * UART longjmp()s back to the test once the scripted input is exhausted.
 * The AUTH challenge is made deterministic (fixed RNG output) so the
 * expected HMAC-style response can be precomputed with the same SHA-256
 * primitives the bootloader itself uses.
 */

#include "eos_bootctl.h"
#include "eos_hal.h"
#include "eos_crypto_boot.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Simulated Flash ---- */

#define SIM_FLASH_SIZE  (128 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];

#define SIM_BOOTCTL_ADDR        0x0000
#define SIM_BOOTCTL_BACKUP_ADDR 0x1000
#define SIM_LOG_ADDR            0x2000
#define SIM_SLOT_A_ADDR         0x4000
#define SIM_SLOT_A_SIZE         0x1000
#define SIM_SLOT_B_ADDR         0x6000
#define SIM_SLOT_B_SIZE         0x1000

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

/* ---- Simulated UART: scripted input, captured output ---- */

static uint8_t script[256];
static size_t script_len = 0;
static size_t script_pos = 0;

static uint8_t out_buf[256];
static size_t out_len = 0;

static jmp_buf exit_jmp;

static int sim_uart_init(uint32_t baud) { (void)baud; return EOS_OK; }

static int sim_uart_send(const void *buf, size_t len)
{
    if (out_len + len > sizeof(out_buf)) len = sizeof(out_buf) - out_len;
    memcpy(&out_buf[out_len], buf, len);
    out_len += len;
    return EOS_OK;
}

static int sim_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (script_pos + len > script_len)
        longjmp(exit_jmp, 1); /* scripted input exhausted: stop the loop */
    memcpy(buf, &script[script_pos], len);
    script_pos += len;
    return EOS_OK;
}

/* ---- Simulated OTP / RNG ---- */

static const uint8_t SIM_SHARED_SECRET[32] = {
    0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
    0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
    0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
    0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
};

static const uint8_t SIM_CHALLENGE[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

static int sim_otp_read(uint32_t offset, void *buf, size_t len)
{
    if (offset != 0x180 || len > sizeof(SIM_SHARED_SECRET))
        return EOS_ERR_GENERIC;
    memcpy(buf, SIM_SHARED_SECRET, len);
    return EOS_OK;
}

static int sim_rng_get(void *buf, size_t len)
{
    if (len > sizeof(SIM_CHALLENGE)) return EOS_ERR_GENERIC;
    memcpy(buf, SIM_CHALLENGE, len);
    return EOS_OK;
}

/* ---- Misc no-op HAL callbacks ---- */

static uint32_t sim_tick = 0;
static uint32_t sim_get_tick(void) { return sim_tick++; }
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t x) { (void)x; }
static void sim_jump(uint32_t addr) { (void)addr; }
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static bool sim_recovery_pin(void) { return false; }
static void sim_system_reset(void) { longjmp(exit_jmp, 1); }

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size           = SIM_FLASH_SIZE,
    .slot_a_addr         = SIM_SLOT_A_ADDR,
    .slot_a_size         = SIM_SLOT_A_SIZE,
    .slot_b_addr         = SIM_SLOT_B_ADDR,
    .slot_b_size         = SIM_SLOT_B_SIZE,
    .recovery_addr       = 0,
    .recovery_size       = 0,
    .bootctl_addr        = SIM_BOOTCTL_ADDR,
    .bootctl_backup_addr = SIM_BOOTCTL_BACKUP_ADDR,
    .log_addr            = SIM_LOG_ADDR,
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
    .uart_init           = sim_uart_init,
    .uart_send           = sim_uart_send,
    .uart_recv           = sim_uart_recv,
    .get_tick_ms         = sim_get_tick,
    .disable_interrupts  = sim_noop,
    .enable_interrupts   = sim_noop,
    .deinit_peripherals  = sim_noop,

    .otp_read            = sim_otp_read,
    .otp_write           = NULL,
    .rng_get             = sim_rng_get,
};

/* ---- Recovery protocol packet (mirrors the private struct in recovery.c) ---- */

#define RCVR_CMD_WRITE 0x04
#define RCVR_CMD_AUTH  0x10
#define RCVR_ACK       0xAA
#define RCVR_NACK      0x55

static void put_pkt(uint8_t *out, uint8_t cmd, uint8_t slot, uint16_t len, uint32_t offset)
{
    out[0] = cmd;
    out[1] = slot;
    out[2] = (uint8_t)(len & 0xFF);
    out[3] = (uint8_t)((len >> 8) & 0xFF);
    out[4] = (uint8_t)(offset & 0xFF);
    out[5] = (uint8_t)((offset >> 8) & 0xFF);
    out[6] = (uint8_t)((offset >> 16) & 0xFF);
    out[7] = (uint8_t)((offset >> 24) & 0xFF);
}

static void script_append(const uint8_t *bytes, size_t len)
{
    memcpy(&script[script_len], bytes, len);
    script_len += len;
}

extern int eos_recovery_enter(eos_bootctl_t *bctl);

/* ---- Test harness ---- */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        setup(); \
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
} while (0)

static void setup(void)
{
    memset(sim_flash, 0xFF, sizeof(sim_flash));
    sim_tick = 0;
    script_len = 0;
    script_pos = 0;
    out_len = 0;
    eos_hal_init(&sim_ops);
}

/* Authenticate, then drive an out-of-bounds WRITE (offset+len past the end
 * of slot A) followed by a well-formed in-bounds WRITE. Before the fix,
 * recovery_handle_write() never validated `offset` against the slot size,
 * so the out-of-bounds write would be ACKed and passed straight to
 * eos_hal_flash_write() — able to corrupt slot B, boot-control, or the
 * boot log. It must now be NACKed, and normal writes must still work. */
TEST(test_write_rejects_offset_past_slot_end)
{
    eos_sha256_ctx_t ctx;
    uint8_t auth_response[32];
    eos_sha256_init(&ctx);
    eos_sha256_update(&ctx, SIM_CHALLENGE, sizeof(SIM_CHALLENGE));
    eos_sha256_update(&ctx, SIM_SHARED_SECRET, sizeof(SIM_SHARED_SECRET));
    eos_sha256_final(&ctx, auth_response);

    uint8_t pkt[8];

    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));           /* -> server sends challenge */

    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));           /* -> server awaits response */
    script_append(auth_response, sizeof(auth_response));

    /* Out-of-bounds write: starts exactly at the slot boundary. */
    put_pkt(pkt, RCVR_CMD_WRITE, EOS_SLOT_A, 16, SIM_SLOT_A_SIZE);
    script_append(pkt, sizeof(pkt));

    /* In-bounds write: must still succeed after the rejection above. */
    put_pkt(pkt, RCVR_CMD_WRITE, EOS_SLOT_A, 4, 0);
    script_append(pkt, sizeof(pkt));
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    script_append(payload, sizeof(payload));

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);

    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl); /* scripted input runs out -> longjmp */
    }

    /* Byte layout of out_buf: [0]=ACK, [1..32]=challenge, [33]=auth ACK,
     * [34]=NACK for the rejected write, [35]=ACK (write accepted),
     * [36]=ACK (write completed). */
    ASSERT(out_len >= 37);
    ASSERT(out_buf[0] == RCVR_ACK);
    ASSERT(out_buf[33] == RCVR_ACK);
    ASSERT(out_buf[34] == RCVR_NACK);
    ASSERT(out_buf[35] == RCVR_ACK);
    ASSERT(out_buf[36] == RCVR_ACK);

    /* The rejected write must not have touched flash at all. */
    ASSERT(sim_flash[SIM_SLOT_A_ADDR + SIM_SLOT_A_SIZE] == 0xFF);
    ASSERT(sim_flash[SIM_SLOT_A_ADDR + SIM_SLOT_A_SIZE + 1] == 0xFF);

    /* The subsequent in-bounds write must have gone through normally. */
    ASSERT(memcmp(&sim_flash[SIM_SLOT_A_ADDR], payload, sizeof(payload)) == 0);
}

int main(void)
{
    printf("=== test_recovery ===\n");
    run_test_write_rejects_offset_past_slot_end();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
