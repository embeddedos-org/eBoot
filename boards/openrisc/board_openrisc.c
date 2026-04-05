// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_openrisc.c
 * @brief OpenRISC mor1kx board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define OPENRISC_FLASH_BASE    0x00000000
#define OPENRISC_FLASH_SIZE    (4*1024*1024)
#define OPENRISC_RAM_BASE      0x00000000
#define OPENRISC_RAM_SIZE      (16*1024*1024)
#define OPENRISC_SLOT_A        (OPENRISC_FLASH_BASE + 0x20000)
#define OPENRISC_SLOT_B        (OPENRISC_FLASH_BASE + OPENRISC_FLASH_SIZE / 3 + 0x20000)
#define OPENRISC_RECOVERY      (OPENRISC_FLASH_BASE + 2 * OPENRISC_FLASH_SIZE / 3)
#define OPENRISC_BOOTCTL       (OPENRISC_FLASH_BASE + OPENRISC_FLASH_SIZE - 0x2000)

static const eos_board_ops_t openrisc_ops = {
    .flash_base       = OPENRISC_FLASH_BASE,
    .flash_size       = OPENRISC_FLASH_SIZE,
    .slot_a_addr      = OPENRISC_SLOT_A,
    .slot_b_addr      = OPENRISC_SLOT_B,
    .recovery_addr    = OPENRISC_RECOVERY,
    .bootctl_addr     = OPENRISC_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_openrisc_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "OpenRISC mor1kx");
    table->board_id = 0xF701;
    table->board_revision = 1;
    table->cpu_clock_hz    = 100000000;
    table->bus_clock_hz    = 100000000;
    table->periph_clock_hz = 100000000;

    eos_mem_region_t flash_region = {
        .base = OPENRISC_FLASH_BASE,
        .size = OPENRISC_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = OPENRISC_RAM_BASE,
        .size = OPENRISC_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_openrisc_get_ops(void)
{
    return &openrisc_ops;
}
