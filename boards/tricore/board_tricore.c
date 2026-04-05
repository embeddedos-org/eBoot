// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_tricore.c
 * @brief AURIX TC397 TriCore board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define TRICORE_FLASH_BASE    0x80000000
#define TRICORE_FLASH_SIZE    (16*1024*1024)
#define TRICORE_RAM_BASE      0x70000000
#define TRICORE_RAM_SIZE      (6*1024*1024)
#define TRICORE_SLOT_A        (TRICORE_FLASH_BASE + 0x20000)
#define TRICORE_SLOT_B        (TRICORE_FLASH_BASE + TRICORE_FLASH_SIZE / 3 + 0x20000)
#define TRICORE_RECOVERY      (TRICORE_FLASH_BASE + 2 * TRICORE_FLASH_SIZE / 3)
#define TRICORE_BOOTCTL       (TRICORE_FLASH_BASE + TRICORE_FLASH_SIZE - 0x2000)

static const eos_board_ops_t tricore_ops = {
    .flash_base       = TRICORE_FLASH_BASE,
    .flash_size       = TRICORE_FLASH_SIZE,
    .slot_a_addr      = TRICORE_SLOT_A,
    .slot_b_addr      = TRICORE_SLOT_B,
    .recovery_addr    = TRICORE_RECOVERY,
    .bootctl_addr     = TRICORE_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_tricore_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "AURIX TC397 TriCore");
    table->board_id = 0xF301;
    table->board_revision = 1;
    table->cpu_clock_hz    = 300000000;
    table->bus_clock_hz    = 300000000;
    table->periph_clock_hz = 300000000;

    eos_mem_region_t flash_region = {
        .base = TRICORE_FLASH_BASE,
        .size = TRICORE_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = TRICORE_RAM_BASE,
        .size = TRICORE_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_tricore_get_ops(void)
{
    return &tricore_ops;
}
