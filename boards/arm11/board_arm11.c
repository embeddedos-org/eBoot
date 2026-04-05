// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arm11.c
 * @brief BCM2835 ARM1176JZF-S board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define ARM11_FLASH_BASE    0x00000000
#define ARM11_FLASH_SIZE    (4*1024*1024)
#define ARM11_RAM_BASE      0x00000000
#define ARM11_RAM_SIZE      (512*1024*1024)
#define ARM11_SLOT_A        (ARM11_FLASH_BASE + 0x20000)
#define ARM11_SLOT_B        (ARM11_FLASH_BASE + ARM11_FLASH_SIZE / 3 + 0x20000)
#define ARM11_RECOVERY      (ARM11_FLASH_BASE + 2 * ARM11_FLASH_SIZE / 3)
#define ARM11_BOOTCTL       (ARM11_FLASH_BASE + ARM11_FLASH_SIZE - 0x2000)

static const eos_board_ops_t arm11_ops = {
    .flash_base       = ARM11_FLASH_BASE,
    .flash_size       = ARM11_FLASH_SIZE,
    .slot_a_addr      = ARM11_SLOT_A,
    .slot_b_addr      = ARM11_SLOT_B,
    .recovery_addr    = ARM11_RECOVERY,
    .bootctl_addr     = ARM11_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_arm11_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "BCM2835 ARM1176JZF-S");
    table->board_id = 0xCB01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 700000000;
    table->bus_clock_hz    = 700000000;
    table->periph_clock_hz = 700000000;

    eos_mem_region_t flash_region = {
        .base = ARM11_FLASH_BASE,
        .size = ARM11_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = ARM11_RAM_BASE,
        .size = ARM11_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_arm11_get_ops(void)
{
    return &arm11_ops;
}
