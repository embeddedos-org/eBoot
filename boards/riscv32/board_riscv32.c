// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_riscv32.c
 * @brief RISC-V 32 ESP32-C3 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define RISCV32_FLASH_BASE    0x00000000
#define RISCV32_FLASH_SIZE    (4*1024*1024)
#define RISCV32_RAM_BASE      0x3FC80000
#define RISCV32_RAM_SIZE      (400*1024)
#define RISCV32_SLOT_A        (RISCV32_FLASH_BASE + 0x20000)
#define RISCV32_SLOT_B        (RISCV32_FLASH_BASE + RISCV32_FLASH_SIZE / 3 + 0x20000)
#define RISCV32_RECOVERY      (RISCV32_FLASH_BASE + 2 * RISCV32_FLASH_SIZE / 3)
#define RISCV32_BOOTCTL       (RISCV32_FLASH_BASE + RISCV32_FLASH_SIZE - 0x2000)

static const eos_board_ops_t riscv32_ops = {
    .flash_base       = RISCV32_FLASH_BASE,
    .flash_size       = RISCV32_FLASH_SIZE,
    .slot_a_addr      = RISCV32_SLOT_A,
    .slot_b_addr      = RISCV32_SLOT_B,
    .recovery_addr    = RISCV32_RECOVERY,
    .bootctl_addr     = RISCV32_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_riscv32_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "RISC-V 32 ESP32-C3");
    table->board_id = 0x0101;
    table->board_revision = 1;
    table->cpu_clock_hz    = 160000000;
    table->bus_clock_hz    = 160000000;
    table->periph_clock_hz = 160000000;

    eos_mem_region_t flash_region = {
        .base = RISCV32_FLASH_BASE,
        .size = RISCV32_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = RISCV32_RAM_BASE,
        .size = RISCV32_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_riscv32_get_ops(void)
{
    return &riscv32_ops;
}
