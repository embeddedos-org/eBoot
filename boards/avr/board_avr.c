// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_avr.c
 * @brief ATmega328P AVR board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define AVR_FLASH_BASE    0x0000
#define AVR_FLASH_SIZE    (32*1024)
#define AVR_RAM_BASE      0x0100
#define AVR_RAM_SIZE      (2*1024)
#define AVR_SLOT_A        (AVR_FLASH_BASE + 0x20000)
#define AVR_SLOT_B        (AVR_FLASH_BASE + AVR_FLASH_SIZE / 3 + 0x20000)
#define AVR_RECOVERY      (AVR_FLASH_BASE + 2 * AVR_FLASH_SIZE / 3)
#define AVR_BOOTCTL       (AVR_FLASH_BASE + AVR_FLASH_SIZE - 0x2000)

static const eos_board_ops_t avr_ops = {
    .flash_base       = AVR_FLASH_BASE,
    .flash_size       = AVR_FLASH_SIZE,
    .slot_a_addr      = AVR_SLOT_A,
    .slot_b_addr      = AVR_SLOT_B,
    .recovery_addr    = AVR_RECOVERY,
    .bootctl_addr     = AVR_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_avr_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "ATmega328P AVR");
    table->board_id = 0xD101;
    table->board_revision = 1;
    table->cpu_clock_hz    = 16000000;
    table->bus_clock_hz    = 16000000;
    table->periph_clock_hz = 16000000;

    eos_mem_region_t flash_region = {
        .base = AVR_FLASH_BASE,
        .size = AVR_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = AVR_RAM_BASE,
        .size = AVR_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_avr_get_ops(void)
{
    return &avr_ops;
}
