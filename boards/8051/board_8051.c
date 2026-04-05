// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_8051.c
 * @brief 8051 STC89C52 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define MCS8051_FLASH_BASE    0x0000
#define MCS8051_FLASH_SIZE    (8*1024)
#define MCS8051_RAM_BASE      0x0000
#define MCS8051_RAM_SIZE      (256)
#define MCS8051_SLOT_A        (MCS8051_FLASH_BASE + 0x20000)
#define MCS8051_SLOT_B        (MCS8051_FLASH_BASE + MCS8051_FLASH_SIZE / 3 + 0x20000)
#define MCS8051_RECOVERY      (MCS8051_FLASH_BASE + 2 * MCS8051_FLASH_SIZE / 3)
#define MCS8051_BOOTCTL       (MCS8051_FLASH_BASE + MCS8051_FLASH_SIZE - 0x2000)

static const eos_board_ops_t 8051_ops = {
    .flash_base       = MCS8051_FLASH_BASE,
    .flash_size       = MCS8051_FLASH_SIZE,
    .slot_a_addr      = MCS8051_SLOT_A,
    .slot_b_addr      = MCS8051_SLOT_B,
    .recovery_addr    = MCS8051_RECOVERY,
    .bootctl_addr     = MCS8051_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_8051_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "8051 STC89C52");
    table->board_id = 0xFF01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 24000000;
    table->bus_clock_hz    = 24000000;
    table->periph_clock_hz = 24000000;

    eos_mem_region_t flash_region = {
        .base = MCS8051_FLASH_BASE,
        .size = MCS8051_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = MCS8051_RAM_BASE,
        .size = MCS8051_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_8051_get_ops(void)
{
    return &8051_ops;
}
