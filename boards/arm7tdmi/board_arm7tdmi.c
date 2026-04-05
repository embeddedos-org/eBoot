// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arm7tdmi.c
 * @brief LPC2148 ARM7TDMI board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define ARM7TDMI_FLASH_BASE    0x00000000
#define ARM7TDMI_FLASH_SIZE    (512*1024)
#define ARM7TDMI_RAM_BASE      0x40000000
#define ARM7TDMI_RAM_SIZE      (40*1024)
#define ARM7TDMI_SLOT_A        (ARM7TDMI_FLASH_BASE + 0x20000)
#define ARM7TDMI_SLOT_B        (ARM7TDMI_FLASH_BASE + ARM7TDMI_FLASH_SIZE / 3 + 0x20000)
#define ARM7TDMI_RECOVERY      (ARM7TDMI_FLASH_BASE + 2 * ARM7TDMI_FLASH_SIZE / 3)
#define ARM7TDMI_BOOTCTL       (ARM7TDMI_FLASH_BASE + ARM7TDMI_FLASH_SIZE - 0x2000)

static const eos_board_ops_t arm7tdmi_ops = {
    .flash_base       = ARM7TDMI_FLASH_BASE,
    .flash_size       = ARM7TDMI_FLASH_SIZE,
    .slot_a_addr      = ARM7TDMI_SLOT_A,
    .slot_b_addr      = ARM7TDMI_SLOT_B,
    .recovery_addr    = ARM7TDMI_RECOVERY,
    .bootctl_addr     = ARM7TDMI_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_arm7tdmi_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "LPC2148 ARM7TDMI");
    table->board_id = 0xC701;
    table->board_revision = 1;
    table->cpu_clock_hz    = 60000000;
    table->bus_clock_hz    = 60000000;
    table->periph_clock_hz = 60000000;

    eos_mem_region_t flash_region = {
        .base = ARM7TDMI_FLASH_BASE,
        .size = ARM7TDMI_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = ARM7TDMI_RAM_BASE,
        .size = ARM7TDMI_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_arm7tdmi_get_ops(void)
{
    return &arm7tdmi_ops;
}
