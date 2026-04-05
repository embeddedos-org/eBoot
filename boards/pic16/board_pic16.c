// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic16.c
 * @brief PIC16F877A board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PIC16_FLASH_BASE    0x0000
#define PIC16_FLASH_SIZE    (14*1024)
#define PIC16_RAM_BASE      0x0020
#define PIC16_RAM_SIZE      (368)
#define PIC16_SLOT_A        (PIC16_FLASH_BASE + 0x20000)
#define PIC16_SLOT_B        (PIC16_FLASH_BASE + PIC16_FLASH_SIZE / 3 + 0x20000)
#define PIC16_RECOVERY      (PIC16_FLASH_BASE + 2 * PIC16_FLASH_SIZE / 3)
#define PIC16_BOOTCTL       (PIC16_FLASH_BASE + PIC16_FLASH_SIZE - 0x2000)

static const eos_board_ops_t pic16_ops = {
    .flash_base       = PIC16_FLASH_BASE,
    .flash_size       = PIC16_FLASH_SIZE,
    .slot_a_addr      = PIC16_SLOT_A,
    .slot_b_addr      = PIC16_SLOT_B,
    .recovery_addr    = PIC16_RECOVERY,
    .bootctl_addr     = PIC16_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pic16_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PIC16F877A");
    table->board_id = 0xD301;
    table->board_revision = 1;
    table->cpu_clock_hz    = 20000000;
    table->bus_clock_hz    = 20000000;
    table->periph_clock_hz = 20000000;

    eos_mem_region_t flash_region = {
        .base = PIC16_FLASH_BASE,
        .size = PIC16_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PIC16_RAM_BASE,
        .size = PIC16_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pic16_get_ops(void)
{
    return &pic16_ops;
}
