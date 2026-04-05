// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arc.c
 * @brief ARC EM9D board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define ARC_FLASH_BASE    0x00000000
#define ARC_FLASH_SIZE    (1024*1024)
#define ARC_RAM_BASE      0x00000000
#define ARC_RAM_SIZE      (256*1024)
#define ARC_SLOT_A        (ARC_FLASH_BASE + 0x20000)
#define ARC_SLOT_B        (ARC_FLASH_BASE + ARC_FLASH_SIZE / 3 + 0x20000)
#define ARC_RECOVERY      (ARC_FLASH_BASE + 2 * ARC_FLASH_SIZE / 3)
#define ARC_BOOTCTL       (ARC_FLASH_BASE + ARC_FLASH_SIZE - 0x2000)

static const eos_board_ops_t arc_ops = {
    .flash_base       = ARC_FLASH_BASE,
    .flash_size       = ARC_FLASH_SIZE,
    .slot_a_addr      = ARC_SLOT_A,
    .slot_b_addr      = ARC_SLOT_B,
    .recovery_addr    = ARC_RECOVERY,
    .bootctl_addr     = ARC_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_arc_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "ARC EM9D");
    table->board_id = 0xFE01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 100000000;
    table->bus_clock_hz    = 100000000;
    table->periph_clock_hz = 100000000;

    eos_mem_region_t flash_region = {
        .base = ARC_FLASH_BASE,
        .size = ARC_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = ARC_RAM_BASE,
        .size = ARC_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_arc_get_ops(void)
{
    return &arc_ops;
}
