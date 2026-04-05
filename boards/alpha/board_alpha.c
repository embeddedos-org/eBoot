// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_alpha.c
 * @brief Alpha 21264 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define ALPHA_FLASH_BASE    0x00000000
#define ALPHA_FLASH_SIZE    (4*1024*1024)
#define ALPHA_RAM_BASE      0x00000000
#define ALPHA_RAM_SIZE      (32768UL*1024*1024)
#define ALPHA_SLOT_A        (ALPHA_FLASH_BASE + 0x20000)
#define ALPHA_SLOT_B        (ALPHA_FLASH_BASE + ALPHA_FLASH_SIZE / 3 + 0x20000)
#define ALPHA_RECOVERY      (ALPHA_FLASH_BASE + 2 * ALPHA_FLASH_SIZE / 3)
#define ALPHA_BOOTCTL       (ALPHA_FLASH_BASE + ALPHA_FLASH_SIZE - 0x2000)

static const eos_board_ops_t alpha_ops = {
    .flash_base       = ALPHA_FLASH_BASE,
    .flash_size       = ALPHA_FLASH_SIZE,
    .slot_a_addr      = ALPHA_SLOT_A,
    .slot_b_addr      = ALPHA_SLOT_B,
    .recovery_addr    = ALPHA_RECOVERY,
    .bootctl_addr     = ALPHA_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_alpha_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Alpha 21264");
    table->board_id = 0x0A01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 1250000000;
    table->bus_clock_hz    = 1250000000;
    table->periph_clock_hz = 1250000000;

    eos_mem_region_t flash_region = {
        .base = ALPHA_FLASH_BASE,
        .size = ALPHA_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = ALPHA_RAM_BASE,
        .size = ALPHA_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_alpha_get_ops(void)
{
    return &alpha_ops;
}
