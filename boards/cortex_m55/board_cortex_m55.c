// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m55.c
 * @brief Corstone-300 Cortex-M55 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define CORTEX_M55_FLASH_BASE    0x10000000
#define CORTEX_M55_FLASH_SIZE    (2*1024*1024)
#define CORTEX_M55_RAM_BASE      0x20000000
#define CORTEX_M55_RAM_SIZE      (512*1024)
#define CORTEX_M55_SLOT_A        (CORTEX_M55_FLASH_BASE + 0x20000)
#define CORTEX_M55_SLOT_B        (CORTEX_M55_FLASH_BASE + CORTEX_M55_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M55_RECOVERY      (CORTEX_M55_FLASH_BASE + 2 * CORTEX_M55_FLASH_SIZE / 3)
#define CORTEX_M55_BOOTCTL       (CORTEX_M55_FLASH_BASE + CORTEX_M55_FLASH_SIZE - 0x2000)

static const eos_board_ops_t cortex_m55_ops = {
    .flash_base       = CORTEX_M55_FLASH_BASE,
    .flash_size       = CORTEX_M55_FLASH_SIZE,
    .slot_a_addr      = CORTEX_M55_SLOT_A,
    .slot_b_addr      = CORTEX_M55_SLOT_B,
    .recovery_addr    = CORTEX_M55_RECOVERY,
    .bootctl_addr     = CORTEX_M55_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_cortex_m55_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Corstone-300 Cortex-M55");
    table->board_id = 0x5501;
    table->board_revision = 1;
    table->cpu_clock_hz    = 250000000;
    table->bus_clock_hz    = 250000000;
    table->periph_clock_hz = 250000000;

    eos_mem_region_t flash_region = {
        .base = CORTEX_M55_FLASH_BASE,
        .size = CORTEX_M55_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = CORTEX_M55_RAM_BASE,
        .size = CORTEX_M55_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_cortex_m55_get_ops(void)
{
    return &cortex_m55_ops;
}
