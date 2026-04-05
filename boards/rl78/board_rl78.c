// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_rl78.c
 * @brief RL78/G14 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define RL78_FLASH_BASE    0x00000
#define RL78_FLASH_SIZE    (512*1024)
#define RL78_RAM_BASE      0xFF700
#define RL78_RAM_SIZE      (48*1024)
#define RL78_SLOT_A        (RL78_FLASH_BASE + 0x20000)
#define RL78_SLOT_B        (RL78_FLASH_BASE + RL78_FLASH_SIZE / 3 + 0x20000)
#define RL78_RECOVERY      (RL78_FLASH_BASE + 2 * RL78_FLASH_SIZE / 3)
#define RL78_BOOTCTL       (RL78_FLASH_BASE + RL78_FLASH_SIZE - 0x2000)

static const eos_board_ops_t rl78_ops = {
    .flash_base       = RL78_FLASH_BASE,
    .flash_size       = RL78_FLASH_SIZE,
    .slot_a_addr      = RL78_SLOT_A,
    .slot_b_addr      = RL78_SLOT_B,
    .recovery_addr    = RL78_RECOVERY,
    .bootctl_addr     = RL78_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_rl78_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "RL78/G14");
    table->board_id = 0xF101;
    table->board_revision = 1;
    table->cpu_clock_hz    = 32000000;
    table->bus_clock_hz    = 32000000;
    table->periph_clock_hz = 32000000;

    eos_mem_region_t flash_region = {
        .base = RL78_FLASH_BASE,
        .size = RL78_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = RL78_RAM_BASE,
        .size = RL78_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_rl78_get_ops(void)
{
    return &rl78_ops;
}
