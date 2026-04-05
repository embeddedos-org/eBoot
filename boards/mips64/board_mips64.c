// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_mips64.c
 * @brief MIPS64 R6 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define MIPS64_FLASH_BASE    0x00000000
#define MIPS64_FLASH_SIZE    (4*1024*1024)
#define MIPS64_RAM_BASE      0x00000000
#define MIPS64_RAM_SIZE      (4096UL*1024*1024)
#define MIPS64_SLOT_A        (MIPS64_FLASH_BASE + 0x20000)
#define MIPS64_SLOT_B        (MIPS64_FLASH_BASE + MIPS64_FLASH_SIZE / 3 + 0x20000)
#define MIPS64_RECOVERY      (MIPS64_FLASH_BASE + 2 * MIPS64_FLASH_SIZE / 3)
#define MIPS64_BOOTCTL       (MIPS64_FLASH_BASE + MIPS64_FLASH_SIZE - 0x2000)

static const eos_board_ops_t mips64_ops = {
    .flash_base       = MIPS64_FLASH_BASE,
    .flash_size       = MIPS64_FLASH_SIZE,
    .slot_a_addr      = MIPS64_SLOT_A,
    .slot_b_addr      = MIPS64_SLOT_B,
    .recovery_addr    = MIPS64_RECOVERY,
    .bootctl_addr     = MIPS64_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_mips64_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "MIPS64 R6");
    table->board_id = 0x0401;
    table->board_revision = 1;
    table->cpu_clock_hz    = 1500000000;
    table->bus_clock_hz    = 1500000000;
    table->periph_clock_hz = 1500000000;

    eos_mem_region_t flash_region = {
        .base = MIPS64_FLASH_BASE,
        .size = MIPS64_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = MIPS64_RAM_BASE,
        .size = MIPS64_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_mips64_get_ops(void)
{
    return &mips64_ops;
}
