// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cris.c
 * @brief ETRAX 100LX board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define CRIS_FLASH_BASE    0x00000000
#define CRIS_FLASH_SIZE    (4*1024*1024)
#define CRIS_RAM_BASE      0x40000000
#define CRIS_RAM_SIZE      (64*1024*1024)
#define CRIS_SLOT_A        (CRIS_FLASH_BASE + 0x20000)
#define CRIS_SLOT_B        (CRIS_FLASH_BASE + CRIS_FLASH_SIZE / 3 + 0x20000)
#define CRIS_RECOVERY      (CRIS_FLASH_BASE + 2 * CRIS_FLASH_SIZE / 3)
#define CRIS_BOOTCTL       (CRIS_FLASH_BASE + CRIS_FLASH_SIZE - 0x2000)

static const eos_board_ops_t cris_ops = {
    .flash_base       = CRIS_FLASH_BASE,
    .flash_size       = CRIS_FLASH_SIZE,
    .slot_a_addr      = CRIS_SLOT_A,
    .slot_b_addr      = CRIS_SLOT_B,
    .recovery_addr    = CRIS_RECOVERY,
    .bootctl_addr     = CRIS_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_cris_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "ETRAX 100LX");
    table->board_id = 0x0C01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 200000000;
    table->bus_clock_hz    = 200000000;
    table->periph_clock_hz = 200000000;

    eos_mem_region_t flash_region = {
        .base = CRIS_FLASH_BASE,
        .size = CRIS_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = CRIS_RAM_BASE,
        .size = CRIS_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_cris_get_ops(void)
{
    return &cris_ops;
}
