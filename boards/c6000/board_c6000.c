// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_c6000.c
 * @brief TMS320C6748 C6000 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define C6000_FLASH_BASE    0x00000000
#define C6000_FLASH_SIZE    (4*1024*1024)
#define C6000_RAM_BASE      0xC0000000
#define C6000_RAM_SIZE      (256*1024*1024)
#define C6000_SLOT_A        (C6000_FLASH_BASE + 0x20000)
#define C6000_SLOT_B        (C6000_FLASH_BASE + C6000_FLASH_SIZE / 3 + 0x20000)
#define C6000_RECOVERY      (C6000_FLASH_BASE + 2 * C6000_FLASH_SIZE / 3)
#define C6000_BOOTCTL       (C6000_FLASH_BASE + C6000_FLASH_SIZE - 0x2000)

static const eos_board_ops_t c6000_ops = {
    .flash_base       = C6000_FLASH_BASE,
    .flash_size       = C6000_FLASH_SIZE,
    .slot_a_addr      = C6000_SLOT_A,
    .slot_b_addr      = C6000_SLOT_B,
    .recovery_addr    = C6000_RECOVERY,
    .bootctl_addr     = C6000_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_c6000_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "TMS320C6748 C6000");
    table->board_id = 0xE301;
    table->board_revision = 1;
    table->cpu_clock_hz    = 456000000;
    table->bus_clock_hz    = 456000000;
    table->periph_clock_hz = 456000000;

    eos_mem_region_t flash_region = {
        .base = C6000_FLASH_BASE,
        .size = C6000_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = C6000_RAM_BASE,
        .size = C6000_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_c6000_get_ops(void)
{
    return &c6000_ops;
}
