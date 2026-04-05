// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_c166.c
 * @brief XC2267M C166 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define C166_FLASH_BASE    0x000000
#define C166_FLASH_SIZE    (768*1024)
#define C166_RAM_BASE      0x00F000
#define C166_RAM_SIZE      (48*1024)
#define C166_SLOT_A        (C166_FLASH_BASE + 0x20000)
#define C166_SLOT_B        (C166_FLASH_BASE + C166_FLASH_SIZE / 3 + 0x20000)
#define C166_RECOVERY      (C166_FLASH_BASE + 2 * C166_FLASH_SIZE / 3)
#define C166_BOOTCTL       (C166_FLASH_BASE + C166_FLASH_SIZE - 0x2000)

static const eos_board_ops_t c166_ops = {
    .flash_base       = C166_FLASH_BASE,
    .flash_size       = C166_FLASH_SIZE,
    .slot_a_addr      = C166_SLOT_A,
    .slot_b_addr      = C166_SLOT_B,
    .recovery_addr    = C166_RECOVERY,
    .bootctl_addr     = C166_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_c166_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "XC2267M C166");
    table->board_id = 0xF401;
    table->board_revision = 1;
    table->cpu_clock_hz    = 80000000;
    table->bus_clock_hz    = 80000000;
    table->periph_clock_hz = 80000000;

    eos_mem_region_t flash_region = {
        .base = C166_FLASH_BASE,
        .size = C166_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = C166_RAM_BASE,
        .size = C166_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_c166_get_ops(void)
{
    return &c166_ops;
}
