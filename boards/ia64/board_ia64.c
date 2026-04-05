// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_ia64.c
 * @brief Itanium 9500 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define IA64_FLASH_BASE    0x00000000
#define IA64_FLASH_SIZE    (4*1024*1024)
#define IA64_RAM_BASE      0x00000000
#define IA64_RAM_SIZE      (524288UL*1024*1024)
#define IA64_SLOT_A        (IA64_FLASH_BASE + 0x20000)
#define IA64_SLOT_B        (IA64_FLASH_BASE + IA64_FLASH_SIZE / 3 + 0x20000)
#define IA64_RECOVERY      (IA64_FLASH_BASE + 2 * IA64_FLASH_SIZE / 3)
#define IA64_BOOTCTL       (IA64_FLASH_BASE + IA64_FLASH_SIZE - 0x2000)

static const eos_board_ops_t ia64_ops = {
    .flash_base       = IA64_FLASH_BASE,
    .flash_size       = IA64_FLASH_SIZE,
    .slot_a_addr      = IA64_SLOT_A,
    .slot_b_addr      = IA64_SLOT_B,
    .recovery_addr    = IA64_RECOVERY,
    .bootctl_addr     = IA64_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_ia64_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Itanium 9500");
    table->board_id = 0x0901;
    table->board_revision = 1;
    table->cpu_clock_hz    = 2530000000;
    table->bus_clock_hz    = 2530000000;
    table->periph_clock_hz = 2530000000;

    eos_mem_region_t flash_region = {
        .base = IA64_FLASH_BASE,
        .size = IA64_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = IA64_RAM_BASE,
        .size = IA64_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_ia64_get_ops(void)
{
    return &ia64_ops;
}
