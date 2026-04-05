// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic18.c
 * @brief PIC18F4550 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PIC18_FLASH_BASE    0x0000
#define PIC18_FLASH_SIZE    (32*1024)
#define PIC18_RAM_BASE      0x0080
#define PIC18_RAM_SIZE      (2*1024)
#define PIC18_SLOT_A        (PIC18_FLASH_BASE + 0x20000)
#define PIC18_SLOT_B        (PIC18_FLASH_BASE + PIC18_FLASH_SIZE / 3 + 0x20000)
#define PIC18_RECOVERY      (PIC18_FLASH_BASE + 2 * PIC18_FLASH_SIZE / 3)
#define PIC18_BOOTCTL       (PIC18_FLASH_BASE + PIC18_FLASH_SIZE - 0x2000)

static const eos_board_ops_t pic18_ops = {
    .flash_base       = PIC18_FLASH_BASE,
    .flash_size       = PIC18_FLASH_SIZE,
    .slot_a_addr      = PIC18_SLOT_A,
    .slot_b_addr      = PIC18_SLOT_B,
    .recovery_addr    = PIC18_RECOVERY,
    .bootctl_addr     = PIC18_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pic18_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PIC18F4550");
    table->board_id = 0xD401;
    table->board_revision = 1;
    table->cpu_clock_hz    = 48000000;
    table->bus_clock_hz    = 48000000;
    table->periph_clock_hz = 48000000;

    eos_mem_region_t flash_region = {
        .base = PIC18_FLASH_BASE,
        .size = PIC18_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PIC18_RAM_BASE,
        .size = PIC18_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pic18_get_ops(void)
{
    return &pic18_ops;
}
