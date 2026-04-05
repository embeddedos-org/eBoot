// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_sharc.c
 * @brief SHARC ADSP-21489 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define SHARC_FLASH_BASE    0x00000000
#define SHARC_FLASH_SIZE    (4*1024*1024)
#define SHARC_RAM_BASE      0x00080000
#define SHARC_RAM_SIZE      (5*1024*1024)
#define SHARC_SLOT_A        (SHARC_FLASH_BASE + 0x20000)
#define SHARC_SLOT_B        (SHARC_FLASH_BASE + SHARC_FLASH_SIZE / 3 + 0x20000)
#define SHARC_RECOVERY      (SHARC_FLASH_BASE + 2 * SHARC_FLASH_SIZE / 3)
#define SHARC_BOOTCTL       (SHARC_FLASH_BASE + SHARC_FLASH_SIZE - 0x2000)

static const eos_board_ops_t sharc_ops = {
    .flash_base       = SHARC_FLASH_BASE,
    .flash_size       = SHARC_FLASH_SIZE,
    .slot_a_addr      = SHARC_SLOT_A,
    .slot_b_addr      = SHARC_SLOT_B,
    .recovery_addr    = SHARC_RECOVERY,
    .bootctl_addr     = SHARC_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_sharc_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "SHARC ADSP-21489");
    table->board_id = 0xFA01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 450000000;
    table->bus_clock_hz    = 450000000;
    table->periph_clock_hz = 450000000;

    eos_mem_region_t flash_region = {
        .base = SHARC_FLASH_BASE,
        .size = SHARC_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = SHARC_RAM_BASE,
        .size = SHARC_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_sharc_get_ops(void)
{
    return &sharc_ops;
}
