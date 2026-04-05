// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_dspic.c
 * @brief dsPIC33FJ256GP710 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define DSPIC_FLASH_BASE    0x000000
#define DSPIC_FLASH_SIZE    (256*1024)
#define DSPIC_RAM_BASE      0x000800
#define DSPIC_RAM_SIZE      (30*1024)
#define DSPIC_SLOT_A        (DSPIC_FLASH_BASE + 0x20000)
#define DSPIC_SLOT_B        (DSPIC_FLASH_BASE + DSPIC_FLASH_SIZE / 3 + 0x20000)
#define DSPIC_RECOVERY      (DSPIC_FLASH_BASE + 2 * DSPIC_FLASH_SIZE / 3)
#define DSPIC_BOOTCTL       (DSPIC_FLASH_BASE + DSPIC_FLASH_SIZE - 0x2000)

static const eos_board_ops_t dspic_ops = {
    .flash_base       = DSPIC_FLASH_BASE,
    .flash_size       = DSPIC_FLASH_SIZE,
    .slot_a_addr      = DSPIC_SLOT_A,
    .slot_b_addr      = DSPIC_SLOT_B,
    .recovery_addr    = DSPIC_RECOVERY,
    .bootctl_addr     = DSPIC_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_dspic_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "dsPIC33FJ256GP710");
    table->board_id = 0xD601;
    table->board_revision = 1;
    table->cpu_clock_hz    = 40000000;
    table->bus_clock_hz    = 40000000;
    table->periph_clock_hz = 40000000;

    eos_mem_region_t flash_region = {
        .base = DSPIC_FLASH_BASE,
        .size = DSPIC_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = DSPIC_RAM_BASE,
        .size = DSPIC_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_dspic_get_ops(void)
{
    return &dspic_ops;
}
