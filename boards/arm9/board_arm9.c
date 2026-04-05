// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arm9.c
 * @brief AT91SAM9G25 ARM926EJ-S board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define ARM9_FLASH_BASE    0x00000000
#define ARM9_FLASH_SIZE    (4*1024*1024)
#define ARM9_RAM_BASE      0x20000000
#define ARM9_RAM_SIZE      (128*1024*1024)
#define ARM9_SLOT_A        (ARM9_FLASH_BASE + 0x20000)
#define ARM9_SLOT_B        (ARM9_FLASH_BASE + ARM9_FLASH_SIZE / 3 + 0x20000)
#define ARM9_RECOVERY      (ARM9_FLASH_BASE + 2 * ARM9_FLASH_SIZE / 3)
#define ARM9_BOOTCTL       (ARM9_FLASH_BASE + ARM9_FLASH_SIZE - 0x2000)

static const eos_board_ops_t arm9_ops = {
    .flash_base       = ARM9_FLASH_BASE,
    .flash_size       = ARM9_FLASH_SIZE,
    .slot_a_addr      = ARM9_SLOT_A,
    .slot_b_addr      = ARM9_SLOT_B,
    .recovery_addr    = ARM9_RECOVERY,
    .bootctl_addr     = ARM9_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_arm9_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "AT91SAM9G25 ARM926EJ-S");
    table->board_id = 0xC901;
    table->board_revision = 1;
    table->cpu_clock_hz    = 400000000;
    table->bus_clock_hz    = 400000000;
    table->periph_clock_hz = 400000000;

    eos_mem_region_t flash_region = {
        .base = ARM9_FLASH_BASE,
        .size = ARM9_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = ARM9_RAM_BASE,
        .size = ARM9_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_arm9_get_ops(void)
{
    return &arm9_ops;
}
