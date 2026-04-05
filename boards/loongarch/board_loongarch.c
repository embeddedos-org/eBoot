// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_loongarch.c
 * @brief Loongson 3A5000 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define LOONGARCH_FLASH_BASE    0x00000000
#define LOONGARCH_FLASH_SIZE    (4*1024*1024)
#define LOONGARCH_RAM_BASE      0x00000000
#define LOONGARCH_RAM_SIZE      (16384UL*1024*1024)
#define LOONGARCH_SLOT_A        (LOONGARCH_FLASH_BASE + 0x20000)
#define LOONGARCH_SLOT_B        (LOONGARCH_FLASH_BASE + LOONGARCH_FLASH_SIZE / 3 + 0x20000)
#define LOONGARCH_RECOVERY      (LOONGARCH_FLASH_BASE + 2 * LOONGARCH_FLASH_SIZE / 3)
#define LOONGARCH_BOOTCTL       (LOONGARCH_FLASH_BASE + LOONGARCH_FLASH_SIZE - 0x2000)

static const eos_board_ops_t loongarch_ops = {
    .flash_base       = LOONGARCH_FLASH_BASE,
    .flash_size       = LOONGARCH_FLASH_SIZE,
    .slot_a_addr      = LOONGARCH_SLOT_A,
    .slot_b_addr      = LOONGARCH_SLOT_B,
    .recovery_addr    = LOONGARCH_RECOVERY,
    .bootctl_addr     = LOONGARCH_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_loongarch_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Loongson 3A5000");
    table->board_id = 0x0701;
    table->board_revision = 1;
    table->cpu_clock_hz    = 2500000000;
    table->bus_clock_hz    = 2500000000;
    table->periph_clock_hz = 2500000000;

    eos_mem_region_t flash_region = {
        .base = LOONGARCH_FLASH_BASE,
        .size = LOONGARCH_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = LOONGARCH_RAM_BASE,
        .size = LOONGARCH_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_loongarch_get_ops(void)
{
    return &loongarch_ops;
}
