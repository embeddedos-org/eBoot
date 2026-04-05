// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_lm32.c
 * @brief LatticeMico32 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define LM32_FLASH_BASE    0x00000000
#define LM32_FLASH_SIZE    (4*1024*1024)
#define LM32_RAM_BASE      0x00000000
#define LM32_RAM_SIZE      (8*1024*1024)
#define LM32_SLOT_A        (LM32_FLASH_BASE + 0x20000)
#define LM32_SLOT_B        (LM32_FLASH_BASE + LM32_FLASH_SIZE / 3 + 0x20000)
#define LM32_RECOVERY      (LM32_FLASH_BASE + 2 * LM32_FLASH_SIZE / 3)
#define LM32_BOOTCTL       (LM32_FLASH_BASE + LM32_FLASH_SIZE - 0x2000)

static const eos_board_ops_t lm32_ops = {
    .flash_base       = LM32_FLASH_BASE,
    .flash_size       = LM32_FLASH_SIZE,
    .slot_a_addr      = LM32_SLOT_A,
    .slot_b_addr      = LM32_SLOT_B,
    .recovery_addr    = LM32_RECOVERY,
    .bootctl_addr     = LM32_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_lm32_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "LatticeMico32");
    table->board_id = 0xF801;
    table->board_revision = 1;
    table->cpu_clock_hz    = 100000000;
    table->bus_clock_hz    = 100000000;
    table->periph_clock_hz = 100000000;

    eos_mem_region_t flash_region = {
        .base = LM32_FLASH_BASE,
        .size = LM32_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = LM32_RAM_BASE,
        .size = LM32_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_lm32_get_ops(void)
{
    return &lm32_ops;
}
