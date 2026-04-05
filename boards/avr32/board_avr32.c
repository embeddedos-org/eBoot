// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_avr32.c
 * @brief AT32UC3A0512 AVR32 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define AVR32_FLASH_BASE    0x80000000
#define AVR32_FLASH_SIZE    (512*1024)
#define AVR32_RAM_BASE      0x00000000
#define AVR32_RAM_SIZE      (64*1024)
#define AVR32_SLOT_A        (AVR32_FLASH_BASE + 0x20000)
#define AVR32_SLOT_B        (AVR32_FLASH_BASE + AVR32_FLASH_SIZE / 3 + 0x20000)
#define AVR32_RECOVERY      (AVR32_FLASH_BASE + 2 * AVR32_FLASH_SIZE / 3)
#define AVR32_BOOTCTL       (AVR32_FLASH_BASE + AVR32_FLASH_SIZE - 0x2000)

static const eos_board_ops_t avr32_ops = {
    .flash_base       = AVR32_FLASH_BASE,
    .flash_size       = AVR32_FLASH_SIZE,
    .slot_a_addr      = AVR32_SLOT_A,
    .slot_b_addr      = AVR32_SLOT_B,
    .recovery_addr    = AVR32_RECOVERY,
    .bootctl_addr     = AVR32_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_avr32_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "AT32UC3A0512 AVR32");
    table->board_id = 0xD201;
    table->board_revision = 1;
    table->cpu_clock_hz    = 66000000;
    table->bus_clock_hz    = 66000000;
    table->periph_clock_hz = 66000000;

    eos_mem_region_t flash_region = {
        .base = AVR32_FLASH_BASE,
        .size = AVR32_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = AVR32_RAM_BASE,
        .size = AVR32_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_avr32_get_ops(void)
{
    return &avr32_ops;
}
