// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_ppc64.c
 * @brief POWER9 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PPC64_FLASH_BASE    0x00000000
#define PPC64_FLASH_SIZE    (4*1024*1024)
#define PPC64_RAM_BASE      0x00000000
#define PPC64_RAM_SIZE      (65536UL*1024*1024)
#define PPC64_SLOT_A        (PPC64_FLASH_BASE + 0x20000)
#define PPC64_SLOT_B        (PPC64_FLASH_BASE + PPC64_FLASH_SIZE / 3 + 0x20000)
#define PPC64_RECOVERY      (PPC64_FLASH_BASE + 2 * PPC64_FLASH_SIZE / 3)
#define PPC64_BOOTCTL       (PPC64_FLASH_BASE + PPC64_FLASH_SIZE - 0x2000)

static const eos_board_ops_t ppc64_ops = {
    .flash_base       = PPC64_FLASH_BASE,
    .flash_size       = PPC64_FLASH_SIZE,
    .slot_a_addr      = PPC64_SLOT_A,
    .slot_b_addr      = PPC64_SLOT_B,
    .recovery_addr    = PPC64_RECOVERY,
    .bootctl_addr     = PPC64_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_ppc64_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "POWER9");
    table->board_id = 0x0601;
    table->board_revision = 1;
    table->cpu_clock_hz    = 4000000000;
    table->bus_clock_hz    = 4000000000;
    table->periph_clock_hz = 4000000000;

    eos_mem_region_t flash_region = {
        .base = PPC64_FLASH_BASE,
        .size = PPC64_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PPC64_RAM_BASE,
        .size = PPC64_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_ppc64_get_ops(void)
{
    return &ppc64_ops;
}
