// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_msp430.c
 * @brief MSP430FR5994 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define MSP430_FLASH_BASE    0x00004400
#define MSP430_FLASH_SIZE    (256*1024)
#define MSP430_RAM_BASE      0x00001C00
#define MSP430_RAM_SIZE      (8*1024)
#define MSP430_SLOT_A        (MSP430_FLASH_BASE + 0x20000)
#define MSP430_SLOT_B        (MSP430_FLASH_BASE + MSP430_FLASH_SIZE / 3 + 0x20000)
#define MSP430_RECOVERY      (MSP430_FLASH_BASE + 2 * MSP430_FLASH_SIZE / 3)
#define MSP430_BOOTCTL       (MSP430_FLASH_BASE + MSP430_FLASH_SIZE - 0x2000)

static const eos_board_ops_t msp430_ops = {
    .flash_base       = MSP430_FLASH_BASE,
    .flash_size       = MSP430_FLASH_SIZE,
    .slot_a_addr      = MSP430_SLOT_A,
    .slot_b_addr      = MSP430_SLOT_B,
    .recovery_addr    = MSP430_RECOVERY,
    .bootctl_addr     = MSP430_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_msp430_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "MSP430FR5994");
    table->board_id = 0xE101;
    table->board_revision = 1;
    table->cpu_clock_hz    = 16000000;
    table->bus_clock_hz    = 16000000;
    table->periph_clock_hz = 16000000;

    eos_mem_region_t flash_region = {
        .base = MSP430_FLASH_BASE,
        .size = MSP430_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = MSP430_RAM_BASE,
        .size = MSP430_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_msp430_get_ops(void)
{
    return &msp430_ops;
}
