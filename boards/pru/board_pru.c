// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pru.c
 * @brief AM335x PRU-ICSS board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PRU_FLASH_BASE    0x00000000
#define PRU_FLASH_SIZE    (8*1024)
#define PRU_RAM_BASE      0x00000000
#define PRU_RAM_SIZE      (16*1024)
#define PRU_SLOT_A        (PRU_FLASH_BASE + 0x20000)
#define PRU_SLOT_B        (PRU_FLASH_BASE + PRU_FLASH_SIZE / 3 + 0x20000)
#define PRU_RECOVERY      (PRU_FLASH_BASE + 2 * PRU_FLASH_SIZE / 3)
#define PRU_BOOTCTL       (PRU_FLASH_BASE + PRU_FLASH_SIZE - 0x2000)

static const eos_board_ops_t pru_ops = {
    .flash_base       = PRU_FLASH_BASE,
    .flash_size       = PRU_FLASH_SIZE,
    .slot_a_addr      = PRU_SLOT_A,
    .slot_b_addr      = PRU_SLOT_B,
    .recovery_addr    = PRU_RECOVERY,
    .bootctl_addr     = PRU_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pru_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "AM335x PRU-ICSS");
    table->board_id = 0xE401;
    table->board_revision = 1;
    table->cpu_clock_hz    = 200000000;
    table->bus_clock_hz    = 200000000;
    table->periph_clock_hz = 200000000;

    eos_mem_region_t flash_region = {
        .base = PRU_FLASH_BASE,
        .size = PRU_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PRU_RAM_BASE,
        .size = PRU_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pru_get_ops(void)
{
    return &pru_ops;
}
