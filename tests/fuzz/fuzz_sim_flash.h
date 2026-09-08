// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fuzz_sim_flash.h
 * @brief RAM-backed board ops, shared by the harnesses that touch flash.
 *
 * eos_image_parse_header(), eos_bootctl_* and eos_fw_update_* all read and
 * write through the HAL, so a harness must install board ops before calling
 * them. Without this every one of those harnesses either dereferences a null
 * op table or fuzzes nothing.
 */

#ifndef EOS_FUZZ_SIM_FLASH_H
#define EOS_FUZZ_SIM_FLASH_H

#include "eos_hal.h"
#include "eos_types.h"

#include <stdbool.h>
#include <string.h>

#define FUZZ_FLASH_SIZE (64 * 1024)

static uint8_t fuzz_flash[FUZZ_FLASH_SIZE];

static int fuzz_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr > FUZZ_FLASH_SIZE || len > FUZZ_FLASH_SIZE - addr) return EOS_ERR_FLASH;
    memcpy(buf, &fuzz_flash[addr], len);
    return EOS_OK;
}
static int fuzz_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (addr > FUZZ_FLASH_SIZE || len > FUZZ_FLASH_SIZE - addr) return EOS_ERR_FLASH;
    memcpy(&fuzz_flash[addr], buf, len);
    return EOS_OK;
}
static int fuzz_flash_erase(uint32_t addr, size_t len)
{
    if (addr > FUZZ_FLASH_SIZE || len > FUZZ_FLASH_SIZE - addr) return EOS_ERR_FLASH;
    memset(&fuzz_flash[addr], 0xFF, len);
    return EOS_OK;
}
static void fuzz_noop(void) {}
static void fuzz_noop_u32(uint32_t v) { (void)v; }
static eos_reset_reason_t fuzz_reset_reason(void) { return EOS_RESET_POWER_ON; }
static void fuzz_system_reset(void) {}
static bool fuzz_recovery_pin(void) { return false; }
static void fuzz_jump(uint32_t a) { (void)a; }

static const eos_board_ops_t fuzz_sim_ops = {
    .flash_base = 0, .flash_size = FUZZ_FLASH_SIZE,
    .slot_a_addr = 0x4000, .slot_a_size = 0x8000,
    .slot_b_addr = 0xC000, .slot_b_size = 0x8000,
    .recovery_addr = 0, .recovery_size = 0,
    .bootctl_addr = 0, .bootctl_backup_addr = 0x1000,
    .log_addr = 0x2000, .app_vector_offset = 0,
    .flash_read = fuzz_flash_read,
    .flash_write = fuzz_flash_write,
    .flash_erase = fuzz_flash_erase,
    .watchdog_init = fuzz_noop_u32,
    .watchdog_feed = fuzz_noop,
    .get_reset_reason = fuzz_reset_reason,
    .system_reset = fuzz_system_reset,
    .recovery_pin_asserted = fuzz_recovery_pin,
    .jump = fuzz_jump,
    .uart_init = NULL, .uart_send = NULL, .uart_recv = NULL,
};

/** Load fuzz input as flash contents at @p addr and install the ops. */
static inline void fuzz_flash_load(uint32_t addr, const uint8_t *data, size_t size)
{
    memset(fuzz_flash, 0xFF, sizeof fuzz_flash);
    if (addr < FUZZ_FLASH_SIZE) {
        size_t n = size;
        if (n > FUZZ_FLASH_SIZE - addr) n = FUZZ_FLASH_SIZE - addr;
        memcpy(&fuzz_flash[addr], data, n);
    }
    eos_hal_init(&fuzz_sim_ops);
}

#endif /* EOS_FUZZ_SIM_FLASH_H */
