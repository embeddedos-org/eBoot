// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_sparc64.c
 * @brief UltraSPARC T2 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define SPARC64_FLASH_BASE    0x00000000
#define SPARC64_FLASH_SIZE    (4*1024*1024)
#define SPARC64_RAM_BASE      0x00000000
#define SPARC64_RAM_SIZE      (32768UL*1024*1024)
#define SPARC64_SLOT_A        (SPARC64_FLASH_BASE + 0x20000)
#define SPARC64_SLOT_B        (SPARC64_FLASH_BASE + SPARC64_FLASH_SIZE / 3 + 0x20000)
#define SPARC64_RECOVERY      (SPARC64_FLASH_BASE + 2 * SPARC64_FLASH_SIZE / 3)
#define SPARC64_BOOTCTL       (SPARC64_FLASH_BASE + SPARC64_FLASH_SIZE - 0x2000)

static const eos_board_ops_t sparc64_ops = {
    .flash_base       = SPARC64_FLASH_BASE,
    .flash_size       = SPARC64_FLASH_SIZE,
    .slot_a_addr      = SPARC64_SLOT_A,
    .slot_b_addr      = SPARC64_SLOT_B,
    .recovery_addr    = SPARC64_RECOVERY,
    .bootctl_addr     = SPARC64_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_sparc64_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "UltraSPARC T2");
    table->board_id = 0x0501;
    table->board_revision = 1;
    table->cpu_clock_hz    = 1400000000;
    table->bus_clock_hz    = 1400000000;
    table->periph_clock_hz = 1400000000;

    eos_mem_region_t flash_region = {
        .base = SPARC64_FLASH_BASE,
        .size = SPARC64_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = SPARC64_RAM_BASE,
        .size = SPARC64_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_sparc64_get_ops(void)
{
    return &sparc64_ops;
}
