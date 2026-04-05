// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_hexagon.c
 * @brief Hexagon SDM845 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define HEXAGON_FLASH_BASE    0x00000000
#define HEXAGON_FLASH_SIZE    (4*1024*1024)
#define HEXAGON_RAM_BASE      0x00000000
#define HEXAGON_RAM_SIZE      (8*1024*1024)
#define HEXAGON_SLOT_A        (HEXAGON_FLASH_BASE + 0x20000)
#define HEXAGON_SLOT_B        (HEXAGON_FLASH_BASE + HEXAGON_FLASH_SIZE / 3 + 0x20000)
#define HEXAGON_RECOVERY      (HEXAGON_FLASH_BASE + 2 * HEXAGON_FLASH_SIZE / 3)
#define HEXAGON_BOOTCTL       (HEXAGON_FLASH_BASE + HEXAGON_FLASH_SIZE - 0x2000)

static const eos_board_ops_t hexagon_ops = {
    .flash_base       = HEXAGON_FLASH_BASE,
    .flash_size       = HEXAGON_FLASH_SIZE,
    .slot_a_addr      = HEXAGON_SLOT_A,
    .slot_b_addr      = HEXAGON_SLOT_B,
    .recovery_addr    = HEXAGON_RECOVERY,
    .bootctl_addr     = HEXAGON_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_hexagon_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Hexagon SDM845");
    table->board_id = 0xFB01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 1500000000;
    table->bus_clock_hz    = 1500000000;
    table->periph_clock_hz = 1500000000;

    eos_mem_region_t flash_region = {
        .base = HEXAGON_FLASH_BASE,
        .size = HEXAGON_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = HEXAGON_RAM_BASE,
        .size = HEXAGON_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_hexagon_get_ops(void)
{
    return &hexagon_ops;
}
