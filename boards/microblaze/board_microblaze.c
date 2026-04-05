// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_microblaze.c
 * @brief MicroBlaze Xilinx board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define MICROBLAZE_FLASH_BASE    0x00000000
#define MICROBLAZE_FLASH_SIZE    (16*1024*1024)
#define MICROBLAZE_RAM_BASE      0x80000000
#define MICROBLAZE_RAM_SIZE      (128*1024*1024)
#define MICROBLAZE_SLOT_A        (MICROBLAZE_FLASH_BASE + 0x20000)
#define MICROBLAZE_SLOT_B        (MICROBLAZE_FLASH_BASE + MICROBLAZE_FLASH_SIZE / 3 + 0x20000)
#define MICROBLAZE_RECOVERY      (MICROBLAZE_FLASH_BASE + 2 * MICROBLAZE_FLASH_SIZE / 3)
#define MICROBLAZE_BOOTCTL       (MICROBLAZE_FLASH_BASE + MICROBLAZE_FLASH_SIZE - 0x2000)

static const eos_board_ops_t microblaze_ops = {
    .flash_base       = MICROBLAZE_FLASH_BASE,
    .flash_size       = MICROBLAZE_FLASH_SIZE,
    .slot_a_addr      = MICROBLAZE_SLOT_A,
    .slot_b_addr      = MICROBLAZE_SLOT_B,
    .recovery_addr    = MICROBLAZE_RECOVERY,
    .bootctl_addr     = MICROBLAZE_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_microblaze_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "MicroBlaze Xilinx");
    table->board_id = 0xF501;
    table->board_revision = 1;
    table->cpu_clock_hz    = 200000000;
    table->bus_clock_hz    = 200000000;
    table->periph_clock_hz = 200000000;

    eos_mem_region_t flash_region = {
        .base = MICROBLAZE_FLASH_BASE,
        .size = MICROBLAZE_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = MICROBLAZE_RAM_BASE,
        .size = MICROBLAZE_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_microblaze_get_ops(void)
{
    return &microblaze_ops;
}
