// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_kalimba.c
 * @brief CSR8675 Kalimba board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define KALIMBA_FLASH_BASE    0x00000000
#define KALIMBA_FLASH_SIZE    (4*1024*1024)
#define KALIMBA_RAM_BASE      0x00000000
#define KALIMBA_RAM_SIZE      (1024*1024)
#define KALIMBA_SLOT_A        (KALIMBA_FLASH_BASE + 0x20000)
#define KALIMBA_SLOT_B        (KALIMBA_FLASH_BASE + KALIMBA_FLASH_SIZE / 3 + 0x20000)
#define KALIMBA_RECOVERY      (KALIMBA_FLASH_BASE + 2 * KALIMBA_FLASH_SIZE / 3)
#define KALIMBA_BOOTCTL       (KALIMBA_FLASH_BASE + KALIMBA_FLASH_SIZE - 0x2000)

static const eos_board_ops_t kalimba_ops = {
    .flash_base       = KALIMBA_FLASH_BASE,
    .flash_size       = KALIMBA_FLASH_SIZE,
    .slot_a_addr      = KALIMBA_SLOT_A,
    .slot_b_addr      = KALIMBA_SLOT_B,
    .recovery_addr    = KALIMBA_RECOVERY,
    .bootctl_addr     = KALIMBA_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_kalimba_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "CSR8675 Kalimba");
    table->board_id = 0x0D01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 120000000;
    table->bus_clock_hz    = 120000000;
    table->periph_clock_hz = 120000000;

    eos_mem_region_t flash_region = {
        .base = KALIMBA_FLASH_BASE,
        .size = KALIMBA_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = KALIMBA_RAM_BASE,
        .size = KALIMBA_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_kalimba_get_ops(void)
{
    return &kalimba_ops;
}
