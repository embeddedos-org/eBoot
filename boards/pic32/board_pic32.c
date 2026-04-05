// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic32.c
 * @brief PIC32MX795F512L board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PIC32_FLASH_BASE    0x9D000000
#define PIC32_FLASH_SIZE    (512*1024)
#define PIC32_RAM_BASE      0xA0000000
#define PIC32_RAM_SIZE      (128*1024)
#define PIC32_SLOT_A        (PIC32_FLASH_BASE + 0x20000)
#define PIC32_SLOT_B        (PIC32_FLASH_BASE + PIC32_FLASH_SIZE / 3 + 0x20000)
#define PIC32_RECOVERY      (PIC32_FLASH_BASE + 2 * PIC32_FLASH_SIZE / 3)
#define PIC32_BOOTCTL       (PIC32_FLASH_BASE + PIC32_FLASH_SIZE - 0x2000)

static const eos_board_ops_t pic32_ops = {
    .flash_base       = PIC32_FLASH_BASE,
    .flash_size       = PIC32_FLASH_SIZE,
    .slot_a_addr      = PIC32_SLOT_A,
    .slot_b_addr      = PIC32_SLOT_B,
    .recovery_addr    = PIC32_RECOVERY,
    .bootctl_addr     = PIC32_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pic32_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PIC32MX795F512L");
    table->board_id = 0xD701;
    table->board_revision = 1;
    table->cpu_clock_hz    = 80000000;
    table->bus_clock_hz    = 80000000;
    table->periph_clock_hz = 80000000;

    eos_mem_region_t flash_region = {
        .base = PIC32_FLASH_BASE,
        .size = PIC32_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PIC32_RAM_BASE,
        .size = PIC32_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pic32_get_ops(void)
{
    return &pic32_ops;
}
