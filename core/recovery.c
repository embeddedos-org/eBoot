// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file recovery.c
 * @brief Recovery mode state machine with challenge-response auth
 *
 * Recovery mode is entered when no valid firmware can be booted,
 * or when explicitly requested. It provides a UART-based protocol
 * for image upload, verification, and device diagnostics.
 *
 * Phase 2: Destructive commands (erase, write, factory reset) require
 * authentication via challenge-response (HMAC-SHA256).
 */

#include "eos_bootctl.h"
#include "eos_image.h"
#include "eos_hal.h"
#include "eos_crypto_boot.h"
#include <string.h>
#include <stdint.h>

/* Recovery protocol commands */
#define RCVR_CMD_PING       0x01
#define RCVR_CMD_INFO       0x02
#define RCVR_CMD_ERASE      0x03
#define RCVR_CMD_WRITE      0x04
#define RCVR_CMD_VERIFY     0x05
#define RCVR_CMD_BOOT       0x06
#define RCVR_CMD_LOG        0x07
#define RCVR_CMD_RESET      0x08
#define RCVR_CMD_FACTORY    0x09
#define RCVR_CMD_AUTH       0x10

#define RCVR_ACK            0xAA
#define RCVR_NACK           0x55

#define RCVR_BAUD_RATE      115200
#define RCVR_TIMEOUT_MS     5000
#define RCVR_WRITE_CHUNK    256

#define RCVR_CHALLENGE_SIZE 32
#define RCVR_MAX_AUTH_FAILS  5
#define RCVR_BACKOFF_BASE_MS 1000

#define RCVR_LOG_MAX_ENTRIES 8

/* Recovery protocol packet header */
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
typedef struct {
    uint8_t  cmd;
    uint8_t  slot;
    uint16_t len;
    uint32_t offset;
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((packed))
#endif
rcvr_packet_t;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

/* Authentication state */
typedef enum {
    RCVR_AUTH_NONE = 0,
    RCVR_AUTH_CHALLENGE_SENT,
    RCVR_AUTH_AUTHENTICATED,
} rcvr_auth_state_t;

static rcvr_auth_state_t auth_state = RCVR_AUTH_NONE;
static uint8_t challenge[RCVR_CHALLENGE_SIZE];
static uint32_t auth_fail_count = 0;

/* Forward declarations from slot_manager */
extern int  eos_slot_scan_all(void);
extern bool eos_slot_is_valid(eos_slot_t slot);
extern int  eos_slot_erase(eos_slot_t slot);

/* Forward declarations from boot_log */
extern void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail);
extern int eos_boot_log_read(uint32_t index, eos_boot_log_entry_t *out);
extern uint32_t eos_boot_log_get_head(void);

static int recovery_send_ack(void)
{
    uint8_t ack = RCVR_ACK;
    return eos_hal_uart_send(&ack, 1);
}

static int recovery_send_nack(void)
{
    uint8_t nack = RCVR_NACK;
    return eos_hal_uart_send(&nack, 1);
}

/**
 * @brief Check if a command requires authentication.
 */
static bool cmd_requires_auth(uint8_t cmd)
{
    switch (cmd) {
    case RCVR_CMD_ERASE:
    case RCVR_CMD_WRITE:
    case RCVR_CMD_VERIFY:
    case RCVR_CMD_BOOT:
    case RCVR_CMD_FACTORY:
    case RCVR_CMD_LOG:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Handle authentication challenge-response.
 */
static int recovery_handle_auth(void)
{
    /* Exponential backoff after failures */
    if (auth_fail_count > 0 && auth_fail_count <= RCVR_MAX_AUTH_FAILS) {
        uint32_t delay_ms = RCVR_BACKOFF_BASE_MS * (1U << (auth_fail_count - 1));
        if (delay_ms > 30000) delay_ms = 30000;
        /* Simple delay using watchdog feed loop */
        uint32_t start = eos_hal_get_tick_ms();
        while ((eos_hal_get_tick_ms() - start) < delay_ms) {
            eos_hal_watchdog_feed();
        }
    }

    if (auth_fail_count >= RCVR_MAX_AUTH_FAILS) {
        return recovery_send_nack();
    }

    if (auth_state == RCVR_AUTH_NONE) {
        /* Generate challenge using RNG */
        int rc = eos_hal_rng_get(challenge, RCVR_CHALLENGE_SIZE);
        if (rc != EOS_OK) {
            /* Fallback: use tick-based pseudo-random */
            uint32_t seed = eos_hal_get_tick_ms();
            for (int i = 0; i < RCVR_CHALLENGE_SIZE; i++) {
                seed = seed * 1103515245 + 12345;
                challenge[i] = (uint8_t)(seed >> 16);
            }
        }

        /* Send challenge to client */
        uint8_t resp[1 + RCVR_CHALLENGE_SIZE];
        resp[0] = RCVR_ACK;
        memcpy(&resp[1], challenge, RCVR_CHALLENGE_SIZE);
        eos_hal_uart_send(resp, sizeof(resp));

        auth_state = RCVR_AUTH_CHALLENGE_SENT;
        return EOS_OK;
    }

    if (auth_state == RCVR_AUTH_CHALLENGE_SENT) {
        /* Receive HMAC-SHA256 response from client */
        uint8_t client_response[EOS_SHA256_DIGEST_SIZE];
        int rc = eos_hal_uart_recv(client_response, EOS_SHA256_DIGEST_SIZE,
                                    RCVR_TIMEOUT_MS);
        if (rc != EOS_OK) {
            auth_state = RCVR_AUTH_NONE;
            return recovery_send_nack();
        }

        /* Compute expected HMAC-SHA256(challenge, shared_secret)
         * For simplicity, use SHA-256(challenge || shared_secret) */
        uint8_t expected[EOS_SHA256_DIGEST_SIZE];
        eos_sha256_ctx_t ctx;
        eos_sha256_init(&ctx);
        eos_sha256_update(&ctx, challenge, RCVR_CHALLENGE_SIZE);

        /* Read shared secret from OTP */
        uint8_t shared_secret[32];
        rc = eos_hal_otp_read(0x180, shared_secret, sizeof(shared_secret));
        if (rc != EOS_OK) {
            /* Fail authentication if OTP secret is unreadable */
            auth_fail_count++;
            auth_state = RCVR_AUTH_NONE;
            eos_boot_log_append(0x21, EOS_SLOT_NONE, auth_fail_count); /* AUTH_FAIL */
            return recovery_send_nack();
        }

        eos_sha256_update(&ctx, shared_secret, sizeof(shared_secret));
        eos_sha256_final(&ctx, expected);

        /* Securely zero the secret */
        volatile uint8_t *p = (volatile uint8_t *)shared_secret;
        for (size_t i = 0; i < sizeof(shared_secret); i++) p[i] = 0;

        /* Constant-time comparison */
        extern int eos_crypto_safe_compare(const uint8_t *a, const uint8_t *b, size_t len);
        if (eos_crypto_safe_compare(client_response, expected,
                                     EOS_SHA256_DIGEST_SIZE) == 0) {
            auth_state = RCVR_AUTH_AUTHENTICATED;
            auth_fail_count = 0;
            eos_boot_log_append(0x20, EOS_SLOT_NONE, 0); /* AUTH_SUCCESS */
            return recovery_send_ack();
        } else {
            auth_fail_count++;
            auth_state = RCVR_AUTH_NONE;
            eos_boot_log_append(0x21, EOS_SLOT_NONE, auth_fail_count); /* AUTH_FAIL */
            return recovery_send_nack();
        }
    }

    return recovery_send_nack();
}

static int recovery_handle_ping(void)
{
    uint8_t response[] = { RCVR_ACK, 'E', 'O', 'S', EOS_BOOTCTL_VERSION };
    return eos_hal_uart_send(response, sizeof(response));
}

static int recovery_handle_info(void)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return recovery_send_nack();

    struct {
        uint8_t  ack;
        uint32_t flash_size;
        uint32_t slot_a_addr;
        uint32_t slot_a_size;
        uint32_t slot_b_addr;
        uint32_t slot_b_size;
    } info;

    info.ack         = RCVR_ACK;
    info.flash_size  = ops->flash_size;
    info.slot_a_addr = ops->slot_a_addr;
    info.slot_a_size = ops->slot_a_size;
    info.slot_b_addr = ops->slot_b_addr;
    info.slot_b_size = ops->slot_b_size;

    return eos_hal_uart_send(&info, sizeof(info));
}

static int recovery_handle_erase(eos_slot_t slot)
{
    if (slot != EOS_SLOT_A && slot != EOS_SLOT_B)
        return recovery_send_nack();

    int rc = eos_slot_erase(slot);
    return (rc == EOS_OK) ? recovery_send_ack() : recovery_send_nack();
}

/**
 * Return EOS_OK if a recovery write of `len` bytes at `offset` stays
 * inside the slot at `base`. Rejects wrap of base+offset.
 * Used by the UART write handler and by host unit tests.
 */
int eos_recovery_write_in_range(uint32_t base, uint32_t slot_size,
                                uint32_t offset, uint16_t len)
{
    if (base == 0 || slot_size == 0 || len == 0)
        return EOS_ERR_INVALID;
    /* Check len first so slot_size - len cannot underflow. */
    if ((uint32_t)len > slot_size || offset > slot_size - (uint32_t)len)
        return EOS_ERR_INVALID;
    if (offset > UINT32_MAX - base)
        return EOS_ERR_INVALID;
    return EOS_OK;
}

static int recovery_handle_write(eos_slot_t slot, uint32_t offset, uint16_t len)
{
    if (slot != EOS_SLOT_A && slot != EOS_SLOT_B)
        return recovery_send_nack();

    uint32_t base = eos_hal_slot_addr(slot);
    uint32_t slot_size = eos_hal_slot_size(slot);

    uint8_t buf[RCVR_WRITE_CHUNK];
    if (len > sizeof(buf))
        return recovery_send_nack();

    /* offset/len come straight from the wire; without this check a
     * recovery client can write past the slot boundary into the other
     * slot, boot-control blocks, or the boot log. */
    uint32_t slot_size = eos_hal_slot_size(slot);
    if (slot_size == 0 || (uint64_t)offset + len > (uint64_t)slot_size)
        return recovery_send_nack();

    recovery_send_ack();

    int rc = eos_hal_uart_recv(buf, len, RCVR_TIMEOUT_MS);
    if (rc != EOS_OK)
        return recovery_send_nack();

    rc = eos_hal_flash_write(base + offset, buf, len);
    return (rc == EOS_OK) ? recovery_send_ack() : recovery_send_nack();
}

static int recovery_handle_verify(eos_slot_t slot)
{
    if (slot != EOS_SLOT_A && slot != EOS_SLOT_B)
        return recovery_send_nack();

    uint32_t addr = eos_hal_slot_addr(slot);
    if (addr == 0)
        return recovery_send_nack();

    eos_image_header_t hdr;
    int rc = eos_image_parse_header(addr, &hdr);
    if (rc != EOS_OK)
        return recovery_send_nack();

    /* eos_image_verify_integrity() adds hdr_size internally — pass base addr only */
    rc = eos_image_verify_integrity(&hdr, addr);
    if (rc != EOS_OK)
        return recovery_send_nack();

    rc = eos_image_verify_signature(&hdr);
    if (rc != EOS_OK)
        return recovery_send_nack();

    return recovery_send_ack();
}

static int recovery_handle_boot(eos_slot_t slot, eos_bootctl_t *bctl)
{
    if (slot != EOS_SLOT_A && slot != EOS_SLOT_B)
        return recovery_send_nack();

    bctl->active_slot = slot;
    bctl->pending_slot = EOS_SLOT_NONE;
    bctl->boot_attempts = 0;
    bctl->flags &= ~EOS_FLAG_FORCE_RECOVERY;
    bctl->flags &= ~EOS_FLAG_FACTORY_RESET;

    int rc = eos_bootctl_save(bctl);
    if (rc != EOS_OK)
        return recovery_send_nack();

    recovery_send_ack();
    eos_hal_system_reset();
    return EOS_OK;
}

static int recovery_handle_factory_reset(eos_bootctl_t *bctl)
{
    int rc1 = eos_slot_erase(EOS_SLOT_A);
    int rc2 = eos_slot_erase(EOS_SLOT_B);
    eos_bootctl_init_defaults(bctl);
    int rc3 = eos_bootctl_save(bctl);
    
    if (rc1 != EOS_OK || rc2 != EOS_OK || rc3 != EOS_OK) {
        return recovery_send_nack();
    }
    
    eos_boot_log_append(EOS_LOG_FACTORY_RESET, EOS_SLOT_NONE, 0);
    return recovery_send_ack();
}

static int recovery_collect_boot_log_entries(
    eos_boot_log_entry_t *entries_out,
    uint16_t *entry_count_out
)
{
    eos_boot_log_entry_t raw_entries[EOS_BOOT_LOG_MAX];
    uint32_t valid_entry_count = 0;
    uint32_t log_head;
    uint16_t written_count = 0;

    if (!entries_out || !entry_count_out)
        return EOS_ERR_INVALID;

    for (uint32_t i = 0; i < EOS_BOOT_LOG_MAX; i++) {
        int rc = eos_boot_log_read(i, &raw_entries[i]);
        if (rc != EOS_OK)
            return rc;

        if (raw_entries[i].event != 0)
            valid_entry_count++;
    }

    log_head = eos_boot_log_get_head() % EOS_BOOT_LOG_MAX;

    for (uint32_t i = 0; i < valid_entry_count; i++) {
        uint32_t entry_index = (valid_entry_count == EOS_BOOT_LOG_MAX) ?
                               ((log_head + i) % EOS_BOOT_LOG_MAX) : i;

        if (raw_entries[entry_index].event == 0)
            continue;

        entries_out[written_count++] = raw_entries[entry_index];
    }

    *entry_count_out = written_count;
    return EOS_OK;
}

static int recovery_handle_boot_log(uint32_t start_index, uint16_t requested_count)
{
    eos_boot_log_entry_t boot_log_entries[EOS_BOOT_LOG_MAX];
    uint16_t total_entries = 0;
    uint16_t response_entry_count;
    int rc;

    if (requested_count == 0)
        requested_count = RCVR_LOG_MAX_ENTRIES;
    if (requested_count > RCVR_LOG_MAX_ENTRIES)
        requested_count = RCVR_LOG_MAX_ENTRIES;

    rc = recovery_collect_boot_log_entries(boot_log_entries, &total_entries);
    if (rc != EOS_OK)
        return recovery_send_nack();
    
    if (start_index >= total_entries) {
        uint8_t empty_response_header[3] = { RCVR_ACK, 0, 0 };
        return eos_hal_uart_send(empty_response_header, sizeof(empty_response_header));
    }

    response_entry_count = (uint16_t)(total_entries - start_index);
    if (response_entry_count > requested_count)
        response_entry_count = requested_count;
    
    uint8_t response_header[3] = {
        RCVR_ACK,
        (uint8_t)(response_entry_count & 0xFF),
        (uint8_t)((response_entry_count >> 8) & 0xFF)
    };

    rc = eos_hal_uart_send(response_header, sizeof(response_header));
    if (rc != EOS_OK)
        return rc;
    
    return eos_hal_uart_send(
        &boot_log_entries[start_index],
        response_entry_count * sizeof(boot_log_entries[0])
    );
}

int eos_recovery_enter(eos_bootctl_t *bctl)
{
    eos_boot_log_append(EOS_LOG_RECOVERY_ENTER, EOS_SLOT_NONE, 0);
    eos_hal_uart_init(RCVR_BAUD_RATE);

    bctl->flags &= ~EOS_FLAG_FORCE_RECOVERY;
    eos_bootctl_save(bctl);

    /* Reset auth state on entry */
    auth_state = RCVR_AUTH_NONE;
    auth_fail_count = 0;

    while (1) {
        eos_hal_watchdog_feed();

        rcvr_packet_t pkt;
        int rc = eos_hal_uart_recv(&pkt, sizeof(pkt), RCVR_TIMEOUT_MS);
        if (rc != EOS_OK)
            continue;

        /* Check authentication for destructive commands */
        if (cmd_requires_auth(pkt.cmd) &&
            auth_state != RCVR_AUTH_AUTHENTICATED) {
            recovery_send_nack();
            continue;
        }

        switch (pkt.cmd) {
        case RCVR_CMD_PING:
            recovery_handle_ping();
            break;

        case RCVR_CMD_INFO:
            recovery_handle_info();
            break;

        case RCVR_CMD_AUTH:
            recovery_handle_auth();
            break;

        case RCVR_CMD_ERASE:
            recovery_handle_erase((eos_slot_t)pkt.slot);
            break;

        case RCVR_CMD_WRITE:
            recovery_handle_write((eos_slot_t)pkt.slot, pkt.offset, pkt.len);
            break;

        case RCVR_CMD_VERIFY:
            recovery_handle_verify((eos_slot_t)pkt.slot);
            break;

        case RCVR_CMD_BOOT:
            recovery_handle_boot((eos_slot_t)pkt.slot, bctl);
            break;

        case RCVR_CMD_RESET:
            recovery_send_ack();
            eos_hal_system_reset();
            break;

        case RCVR_CMD_FACTORY:
            recovery_handle_factory_reset(bctl);
            break;

        case RCVR_CMD_LOG:
            recovery_handle_boot_log(pkt.offset, pkt.len);
            break;

        default:
            recovery_send_nack();
            break;
        }
    }

    return EOS_OK;
}
