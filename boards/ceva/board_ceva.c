// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_ceva.c
 * @brief CEVA-XM6 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define CEVA_FLASH_BASE    0x00000000
#define CEVA_FLASH_SIZE    (4*1024*1024)
#define CEVA_RAM_BASE      0x00000000
#define CEVA_RAM_SIZE      (4*1024*1024)
#define CEVA_SLOT_A        (CEVA_FLASH_BASE + 0x20000)
#define CEVA_SLOT_B        (CEVA_FLASH_BASE + CEVA_FLASH_SIZE / 3 + 0x20000)
#define CEVA_RECOVERY      (CEVA_FLASH_BASE + 2 * CEVA_FLASH_SIZE / 3)
#define CEVA_BOOTCTL       (CEVA_FLASH_BASE + CEVA_FLASH_SIZE - 0x2000)

static const eos_board_ops_t ceva_ops = {
    .flash_base       = CEVA_FLASH_BASE,
    .flash_size       = CEVA_FLASH_SIZE,
    .slot_a_addr      = CEVA_SLOT_A,
    .slot_b_addr      = CEVA_SLOT_B,
    .recovery_addr    = CEVA_RECOVERY,
    .bootctl_addr     = CEVA_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_ceva_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "CEVA-XM6");
    table->board_id = 0xFC01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 1000000000;
    table->bus_clock_hz    = 1000000000;
    table->periph_clock_hz = 1000000000;

    eos_mem_region_t flash_region = {
        .base = CEVA_FLASH_BASE,
        .size = CEVA_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = CEVA_RAM_BASE,
        .size = CEVA_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_ceva_get_ops(void)
{
    return &ceva_ops;
}
