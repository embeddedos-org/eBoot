// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_rx.c
 * @brief RX65N board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define RX_FLASH_BASE    0xFFE00000
#define RX_FLASH_SIZE    (2*1024*1024)
#define RX_RAM_BASE      0x00000000
#define RX_RAM_SIZE      (640*1024)
#define RX_SLOT_A        (RX_FLASH_BASE + 0x20000)
#define RX_SLOT_B        (RX_FLASH_BASE + RX_FLASH_SIZE / 3 + 0x20000)
#define RX_RECOVERY      (RX_FLASH_BASE + 2 * RX_FLASH_SIZE / 3)
#define RX_BOOTCTL       (RX_FLASH_BASE + RX_FLASH_SIZE - 0x2000)

static const eos_board_ops_t rx_ops = {
    .flash_base       = RX_FLASH_BASE,
    .flash_size       = RX_FLASH_SIZE,
    .slot_a_addr      = RX_SLOT_A,
    .slot_b_addr      = RX_SLOT_B,
    .recovery_addr    = RX_RECOVERY,
    .bootctl_addr     = RX_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_rx_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "RX65N");
    table->board_id = 0xF201;
    table->board_revision = 1;
    table->cpu_clock_hz    = 120000000;
    table->bus_clock_hz    = 120000000;
    table->periph_clock_hz = 120000000;

    eos_mem_region_t flash_region = {
        .base = RX_FLASH_BASE,
        .size = RX_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = RX_RAM_BASE,
        .size = RX_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_rx_get_ops(void)
{
    return &rx_ops;
}
