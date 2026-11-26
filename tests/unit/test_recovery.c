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
#include "eos_recovery.h"
#include "eos_hal.h"
#include "eos_crypto_boot.h"
#include "eos_image.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "eos_boot_log.h"

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

static size_t verify_payload_bytes_read = 0;

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr], len);
    if (addr >= SIM_SLOT_A_ADDR + sizeof(eos_image_header_t) &&
        addr < SIM_SLOT_B_ADDR)
        verify_payload_bytes_read += len;
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

/* What the simulated OTP holds at the recovery-secret offset. Tests point it
 * at an unprogrammed pattern to show that such a device authenticates nobody. */
static const uint8_t *sim_secret = SIM_SHARED_SECRET;
static const uint8_t SIM_SECRET_ALL_ZERO[32] = { 0 };
static const uint8_t SIM_SECRET_ALL_ONES[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static int sim_otp_read(uint32_t offset, void *buf, size_t len)
{
    if (offset != 0x180 || len > sizeof(SIM_SHARED_SECRET))
        return EOS_ERR_GENERIC;
    memcpy(buf, sim_secret, len);
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
#define RCVR_CMD_VERIFY 0x05
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
extern int eos_recovery_write_in_range(uint32_t base, uint32_t slot_size,
                                       uint32_t offset, uint16_t len);

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
    verify_payload_bytes_read = 0;
    sim_secret = SIM_SHARED_SECRET;
    eos_hal_init(&sim_ops);
}

/* Script the two-packet authentication exchange, computing the response the
 * way a client that knows `secret` would. */
static void script_auth(const uint8_t *secret)
{
    eos_sha256_ctx_t ctx;
    uint8_t auth_response[32];
    eos_sha256_init(&ctx);
    eos_sha256_update(&ctx, SIM_CHALLENGE, sizeof(SIM_CHALLENGE));
    eos_sha256_update(&ctx, secret, 32);
    eos_sha256_final(&ctx, auth_response);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));           /* -> server sends challenge */
    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));           /* -> server awaits response */
    script_append(auth_response, sizeof(auth_response));
}

/* A device whose recovery secret was never programmed reads its fuses back
 * as all zeros or all ones. Both are public, so a client that knows nothing
 * can compute the "right" response. The server must refuse it -- and, having
 * refused, must still refuse the write that follows. */
static void check_unprovisioned_secret_is_refused(const uint8_t *pattern)
{
    setup();
    sim_secret = pattern;

    script_auth(pattern);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_WRITE, EOS_SLOT_A, 4, 0);
    script_append(pkt, sizeof(pkt));
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    script_append(payload, sizeof(payload));

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    /* [0]=ACK, [1..32]=challenge, [33]=auth verdict, [34]=write verdict */
    ASSERT(out_len >= 35);
    ASSERT(out_buf[0] == RCVR_ACK);
    ASSERT(out_buf[33] == RCVR_NACK);
    ASSERT(out_buf[34] == RCVR_NACK);
    ASSERT(sim_flash[SIM_SLOT_A_ADDR] == 0xFF);
}

TEST(test_auth_refuses_an_unprovisioned_secret)
{
    check_unprovisioned_secret_is_refused(SIM_SECRET_ALL_ZERO);
    check_unprovisioned_secret_is_refused(SIM_SECRET_ALL_ONES);
}

/* The control for the test above: the same exchange with a provisioned
 * secret authenticates and the write goes through, so the refusal is about
 * the secret's value and not about the exchange. */
TEST(test_auth_accepts_a_provisioned_secret)
{
    script_auth(SIM_SHARED_SECRET);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_WRITE, EOS_SLOT_A, 4, 0);
    script_append(pkt, sizeof(pkt));
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    script_append(payload, sizeof(payload));

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    ASSERT(out_len >= 36);
    ASSERT(out_buf[33] == RCVR_ACK);
    ASSERT(out_buf[34] == RCVR_ACK);
    ASSERT(out_buf[35] == RCVR_ACK);
    ASSERT(memcmp(&sim_flash[SIM_SLOT_A_ADDR], payload, sizeof(payload)) == 0);
}

/* A board that leaves slot B unmapped reports base 0 for it. The write
 * handler used to check only offset + len against the slot size, so a
 * write "at offset 0x800 of slot B" landed at flash address 0x800 -- in
 * this layout, between the boot-control block and its backup. The shared
 * range rule refuses an unmapped slot; the handler has to use it. */
TEST(test_write_refuses_a_slot_the_board_leaves_unmapped)
{
    eos_board_ops_t unmapped_b = sim_ops;
    unmapped_b.slot_b_addr = 0;
    unmapped_b.slot_b_size = SIM_SLOT_B_SIZE;
    eos_hal_init(&unmapped_b);

    script_auth(SIM_SHARED_SECRET);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_WRITE, EOS_SLOT_B, 4, 0x800);
    script_append(pkt, sizeof(pkt));
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    script_append(payload, sizeof(payload));   /* only consumed if accepted */

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    ASSERT(out_len >= 35);
    ASSERT(out_buf[33] == RCVR_ACK);       /* authenticated */
    ASSERT(out_buf[34] == RCVR_NACK);      /* write refused */
    ASSERT(sim_flash[0x800] == 0xFF);
    ASSERT(sim_flash[0x801] == 0xFF);
}

TEST(test_write_range_helper_rejects_invalid_bounds)
{
    ASSERT(eos_recovery_write_in_range(SIM_SLOT_A_ADDR, SIM_SLOT_A_SIZE,
                                       0, 1) == EOS_OK);
    ASSERT(eos_recovery_write_in_range(SIM_SLOT_A_ADDR, SIM_SLOT_A_SIZE,
                                       SIM_SLOT_A_SIZE - 16, 16) == EOS_OK);

    ASSERT(eos_recovery_write_in_range(SIM_SLOT_A_ADDR, SIM_SLOT_A_SIZE,
                                       SIM_SLOT_A_SIZE, 1) == EOS_ERR_INVALID);
    ASSERT(eos_recovery_write_in_range(SIM_SLOT_A_ADDR, SIM_SLOT_A_SIZE,
                                       SIM_SLOT_A_SIZE - 15, 16) == EOS_ERR_INVALID);
    ASSERT(eos_recovery_write_in_range(0, SIM_SLOT_A_SIZE,
                                       0, 1) == EOS_ERR_INVALID);
    ASSERT(eos_recovery_write_in_range(SIM_SLOT_A_ADDR, 0,
                                       0, 1) == EOS_ERR_INVALID);
    ASSERT(eos_recovery_write_in_range(SIM_SLOT_A_ADDR, SIM_SLOT_A_SIZE,
                                       0, 0) == EOS_ERR_INVALID);

    ASSERT(eos_recovery_write_in_range(0xFFFFFFF0u, 0x100u,
                                       0x20u, 1) == EOS_ERR_INVALID);
    ASSERT(eos_recovery_write_in_range(0xFFFFFFF0u, 0x100u,
                                       0, 17) == EOS_ERR_INVALID);
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

/* Without an entropy source there is no challenge worth sending. The old
 * fallback derived all 32 bytes from the millisecond tick at the moment the
 * AUTH command was handled, so a client that had captured one (challenge,
 * response) pair could reset the board and retry -- seeing each challenge
 * before having to answer it, at no cost in failure count -- until the
 * captured one came back. No shipped board port provides rng_get, so this is
 * every board in the tree. The server must refuse the AUTH outright, log why,
 * and keep refusing the write that follows. */
TEST(test_auth_refuses_when_the_board_has_no_entropy_source)
{
    eos_board_ops_t no_rng = sim_ops;
    no_rng.rng_get = NULL;
    eos_hal_init(&no_rng);
    eos_boot_log_init(0);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));           /* -> must be refused, not answered */
    put_pkt(pkt, RCVR_CMD_WRITE, EOS_SLOT_A, 4, 0);
    script_append(pkt, sizeof(pkt));
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    script_append(payload, sizeof(payload));   /* only consumed if accepted */

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    /* [0]=auth verdict, [1]=write verdict: no 32-byte challenge in between. */
    ASSERT(out_len >= 2);
    ASSERT(out_buf[0] == RCVR_NACK);
    ASSERT(out_buf[1] == RCVR_NACK);
    ASSERT(sim_flash[SIM_SLOT_A_ADDR] == 0xFF);

    /* The refusal is recorded as EOS_LOG_AUTH_NO_ENTROPY, not as a client
     * failure, so a field log tells the integrator what is missing. */
    bool logged = false;
    for (uint32_t i = 0; i < eos_boot_log_get_head(); i++) {
        eos_boot_log_entry_t entry;
        ASSERT(eos_boot_log_read(i, &entry) == EOS_OK);
        if (entry.event == EOS_LOG_AUTH_NO_ENTROPY) logged = true;
    }
    ASSERT(logged);
}

static void fill_header(eos_image_header_t *hdr, uint32_t image_size)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = EOS_IMG_MAGIC;
    hdr->hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr->hdr_size = (uint16_t)sizeof(eos_image_header_t);
    hdr->image_size = image_size;
    hdr->load_addr = 0x20000000;
    hdr->entry_addr = 0x20000100;
}

/* VERIFY must reject an image whose payload exceeds the slot, instead of
 * streaming reads past the slot boundary during eos_image_verify_integrity(). */
TEST(test_verify_rejects_oversized_image_before_reading_payload)
{
    eos_image_header_t hdr;
    fill_header(&hdr, SIM_SLOT_A_SIZE + 0x400u);
    memcpy(&sim_flash[SIM_SLOT_A_ADDR], &hdr, sizeof(hdr));

    eos_sha256_ctx_t ctx;
    uint8_t auth_response[32];
    eos_sha256_init(&ctx);
    eos_sha256_update(&ctx, SIM_CHALLENGE, sizeof(SIM_CHALLENGE));
    eos_sha256_update(&ctx, SIM_SHARED_SECRET, sizeof(SIM_SHARED_SECRET));
    eos_sha256_final(&ctx, auth_response);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));
    put_pkt(pkt, RCVR_CMD_AUTH, 0, 0, 0);
    script_append(pkt, sizeof(pkt));
    script_append(auth_response, sizeof(auth_response));

    put_pkt(pkt, RCVR_CMD_VERIFY, EOS_SLOT_A, 0, 0);
    script_append(pkt, sizeof(pkt));

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);

    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    /* auth ACK then VERIFY NACK */
    ASSERT(out_len >= 35);
    ASSERT(out_buf[33] == RCVR_ACK);
    ASSERT(out_buf[34] == RCVR_NACK);
    ASSERT(verify_payload_bytes_read == 0);
}

#define RCVR_CMD_INFO 0x02
#define RCVR_CAP_RNG  0x01
#define RCVR_CAP_OTP  0x02
#define INFO_WIRE_LEN 22   /* ack(1) + five uint32 + caps(1), packed */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Drive RCVR_CMD_INFO -- which needs no authentication -- and decode the
 * response exactly as tools/uart_recovery.py does. Before the fix the struct was
 * unpacked (24 bytes, three of padding after ack that nothing wrote) and
 * sizeof(info) sent all of it, so an unauthenticated caller received three
 * bytes of whatever the previous call left on the stack, and the client
 * decoded them as the flash size. out_buf is poisoned first so any byte the
 * firmware does not write shows as 0xA5 rather than as a lucky zero. */
TEST(test_info_sends_the_packed_layout_the_client_parses_and_nothing_else)
{
    memset(out_buf, 0xA5, sizeof(out_buf));
    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_INFO, 0, 0, 0);
    script_append(pkt, sizeof(pkt));

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    ASSERT(out_len == INFO_WIRE_LEN);           /* not 24: no padding on the wire */
    ASSERT(out_buf[0] == RCVR_ACK);
    ASSERT(le32(&out_buf[1])  == SIM_FLASH_SIZE);   /* was 0x00A5A5A5 -- the leak */
    ASSERT(le32(&out_buf[5])  == SIM_SLOT_A_ADDR);
    ASSERT(le32(&out_buf[9])  == SIM_SLOT_A_SIZE);
    ASSERT(le32(&out_buf[13]) == SIM_SLOT_B_ADDR);
    ASSERT(le32(&out_buf[17]) == SIM_SLOT_B_SIZE);
    ASSERT(out_buf[21] == (RCVR_CAP_RNG | RCVR_CAP_OTP));   /* sim_ops has both */
    ASSERT(out_buf[22] == 0xA5);                 /* the byte after: untouched */
}

/* The capability byte is the answer to "why does AUTH always NACK?" on a board
 * with no entropy source, delivered before the 15 s of backoff it used to cost
 * to find out. It is derived from the ops table, not configured. */
TEST(test_info_reports_a_board_without_an_entropy_source)
{
    eos_board_ops_t no_rng = sim_ops;
    no_rng.rng_get = NULL;
    eos_hal_init(&no_rng);

    uint8_t pkt[8];
    put_pkt(pkt, RCVR_CMD_INFO, 0, 0, 0);
    script_append(pkt, sizeof(pkt));
    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    ASSERT(out_len == INFO_WIRE_LEN);
    ASSERT((out_buf[21] & RCVR_CAP_RNG) == 0);
    ASSERT((out_buf[21] & RCVR_CAP_OTP) != 0);   /* OTP is still there */
}

/* eos_fw_factory_reset() sets EOS_FLAG_FACTORY_RESET and documents that the
 * next boot "erases both slots and resets boot control to defaults". The
 * flag routed the boot into recovery and nothing else: the slots were left
 * as they were, and the flag was never cleared, so every boot after that
 * came back to recovery. */
TEST(test_requested_factory_reset_is_carried_out_on_entry)
{
    memset(&sim_flash[SIM_SLOT_A_ADDR], 0x5A, SIM_SLOT_A_SIZE);
    memset(&sim_flash[SIM_SLOT_B_ADDR], 0x5A, SIM_SLOT_B_SIZE);

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    bctl.active_slot = EOS_SLOT_B;
    bctl.boot_count = 42;
    ASSERT(eos_bootctl_request_factory_reset(&bctl) == EOS_OK);

    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    for (uint32_t i = 0; i < SIM_SLOT_A_SIZE; i++)
        ASSERT(sim_flash[SIM_SLOT_A_ADDR + i] == 0xFF);
    for (uint32_t i = 0; i < SIM_SLOT_B_SIZE; i++)
        ASSERT(sim_flash[SIM_SLOT_B_ADDR + i] == 0xFF);

    /* The persisted control block is back to defaults, request cleared. */
    eos_bootctl_t saved;
    ASSERT(eos_bootctl_load(&saved) == EOS_OK);
    ASSERT((saved.flags & EOS_FLAG_FACTORY_RESET) == 0);
    ASSERT(saved.active_slot == EOS_SLOT_A);
    ASSERT(saved.boot_count == 0);
}

/* Only a factory-reset request erases anything: an ordinary entry into
 * recovery leaves both slots alone. */
TEST(test_plain_recovery_entry_erases_nothing)
{
    memset(&sim_flash[SIM_SLOT_A_ADDR], 0x5A, SIM_SLOT_A_SIZE);
    memset(&sim_flash[SIM_SLOT_B_ADDR], 0x5A, SIM_SLOT_B_SIZE);

    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    ASSERT(eos_bootctl_request_recovery(&bctl) == EOS_OK);

    if (setjmp(exit_jmp) == 0) {
        eos_recovery_enter(&bctl);
    }

    ASSERT(sim_flash[SIM_SLOT_A_ADDR] == 0x5A);
    ASSERT(sim_flash[SIM_SLOT_B_ADDR] == 0x5A);
}

int main(void)
{
    printf("=== test_recovery ===\n");
    run_test_write_range_helper_rejects_invalid_bounds();
    run_test_write_rejects_offset_past_slot_end();
    run_test_auth_refuses_an_unprovisioned_secret();
    run_test_auth_accepts_a_provisioned_secret();
    run_test_write_refuses_a_slot_the_board_leaves_unmapped();
    run_test_auth_refuses_when_the_board_has_no_entropy_source();
    run_test_verify_rejects_oversized_image_before_reading_payload();
    run_test_info_sends_the_packed_layout_the_client_parses_and_nothing_else();
    run_test_info_reports_a_board_without_an_entropy_source();
    run_test_requested_factory_reset_is_carried_out_on_entry();
    run_test_plain_recovery_entry_erases_nothing();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
