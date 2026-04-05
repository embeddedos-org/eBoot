// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_nios2.c
 * @brief Nios II Intel/Altera board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define NIOS2_FLASH_BASE    0x00000000
#define NIOS2_FLASH_SIZE    (16*1024*1024)
#define NIOS2_RAM_BASE      0x00000000
#define NIOS2_RAM_SIZE      (128*1024*1024)
#define NIOS2_SLOT_A        (NIOS2_FLASH_BASE + 0x20000)
#define NIOS2_SLOT_B        (NIOS2_FLASH_BASE + NIOS2_FLASH_SIZE / 3 + 0x20000)
#define NIOS2_RECOVERY      (NIOS2_FLASH_BASE + 2 * NIOS2_FLASH_SIZE / 3)
#define NIOS2_BOOTCTL       (NIOS2_FLASH_BASE + NIOS2_FLASH_SIZE - 0x2000)

static const eos_board_ops_t nios2_ops = {
    .flash_base       = NIOS2_FLASH_BASE,
    .flash_size       = NIOS2_FLASH_SIZE,
    .slot_a_addr      = NIOS2_SLOT_A,
    .slot_b_addr      = NIOS2_SLOT_B,
    .recovery_addr    = NIOS2_RECOVERY,
    .bootctl_addr     = NIOS2_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_nios2_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Nios II Intel/Altera");
    table->board_id = 0xF601;
    table->board_revision = 1;
    table->cpu_clock_hz    = 250000000;
    table->bus_clock_hz    = 250000000;
    table->periph_clock_hz = 250000000;

    eos_mem_region_t flash_region = {
        .base = NIOS2_FLASH_BASE,
        .size = NIOS2_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = NIOS2_RAM_BASE,
        .size = NIOS2_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_nios2_get_ops(void)
{
    return &nios2_ops;
}
