// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_parisc.c
 * @brief PA-8700 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PARISC_FLASH_BASE    0x00000000
#define PARISC_FLASH_SIZE    (4*1024*1024)
#define PARISC_RAM_BASE      0x00000000
#define PARISC_RAM_SIZE      (4096UL*1024*1024)
#define PARISC_SLOT_A        (PARISC_FLASH_BASE + 0x20000)
#define PARISC_SLOT_B        (PARISC_FLASH_BASE + PARISC_FLASH_SIZE / 3 + 0x20000)
#define PARISC_RECOVERY      (PARISC_FLASH_BASE + 2 * PARISC_FLASH_SIZE / 3)
#define PARISC_BOOTCTL       (PARISC_FLASH_BASE + PARISC_FLASH_SIZE - 0x2000)

static const eos_board_ops_t parisc_ops = {
    .flash_base       = PARISC_FLASH_BASE,
    .flash_size       = PARISC_FLASH_SIZE,
    .slot_a_addr      = PARISC_SLOT_A,
    .slot_b_addr      = PARISC_SLOT_B,
    .recovery_addr    = PARISC_RECOVERY,
    .bootctl_addr     = PARISC_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_parisc_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PA-8700");
    table->board_id = 0x0801;
    table->board_revision = 1;
    table->cpu_clock_hz    = 875000000;
    table->bus_clock_hz    = 875000000;
    table->periph_clock_hz = 875000000;

    eos_mem_region_t flash_region = {
        .base = PARISC_FLASH_BASE,
        .size = PARISC_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PARISC_RAM_BASE,
        .size = PARISC_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_parisc_get_ops(void)
{
    return &parisc_ops;
}
