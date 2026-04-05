// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic24.c
 * @brief PIC24FJ128GA010 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PIC24_FLASH_BASE    0x000000
#define PIC24_FLASH_SIZE    (128*1024)
#define PIC24_RAM_BASE      0x000800
#define PIC24_RAM_SIZE      (8*1024)
#define PIC24_SLOT_A        (PIC24_FLASH_BASE + 0x20000)
#define PIC24_SLOT_B        (PIC24_FLASH_BASE + PIC24_FLASH_SIZE / 3 + 0x20000)
#define PIC24_RECOVERY      (PIC24_FLASH_BASE + 2 * PIC24_FLASH_SIZE / 3)
#define PIC24_BOOTCTL       (PIC24_FLASH_BASE + PIC24_FLASH_SIZE - 0x2000)

static const eos_board_ops_t pic24_ops = {
    .flash_base       = PIC24_FLASH_BASE,
    .flash_size       = PIC24_FLASH_SIZE,
    .slot_a_addr      = PIC24_SLOT_A,
    .slot_b_addr      = PIC24_SLOT_B,
    .recovery_addr    = PIC24_RECOVERY,
    .bootctl_addr     = PIC24_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pic24_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PIC24FJ128GA010");
    table->board_id = 0xD501;
    table->board_revision = 1;
    table->cpu_clock_hz    = 32000000;
    table->bus_clock_hz    = 32000000;
    table->periph_clock_hz = 32000000;

    eos_mem_region_t flash_region = {
        .base = PIC24_FLASH_BASE,
        .size = PIC24_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PIC24_RAM_BASE,
        .size = PIC24_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pic24_get_ops(void)
{
    return &pic24_ops;
}
