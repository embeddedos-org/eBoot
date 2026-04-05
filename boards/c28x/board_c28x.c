// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_c28x.c
 * @brief TMS320F28379D C28x board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define C28X_FLASH_BASE    0x00080000
#define C28X_FLASH_SIZE    (1024*1024)
#define C28X_RAM_BASE      0x00000000
#define C28X_RAM_SIZE      (200*1024)
#define C28X_SLOT_A        (C28X_FLASH_BASE + 0x20000)
#define C28X_SLOT_B        (C28X_FLASH_BASE + C28X_FLASH_SIZE / 3 + 0x20000)
#define C28X_RECOVERY      (C28X_FLASH_BASE + 2 * C28X_FLASH_SIZE / 3)
#define C28X_BOOTCTL       (C28X_FLASH_BASE + C28X_FLASH_SIZE - 0x2000)

static const eos_board_ops_t c28x_ops = {
    .flash_base       = C28X_FLASH_BASE,
    .flash_size       = C28X_FLASH_SIZE,
    .slot_a_addr      = C28X_SLOT_A,
    .slot_b_addr      = C28X_SLOT_B,
    .recovery_addr    = C28X_RECOVERY,
    .bootctl_addr     = C28X_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_c28x_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "TMS320F28379D C28x");
    table->board_id = 0xE201;
    table->board_revision = 1;
    table->cpu_clock_hz    = 200000000;
    table->bus_clock_hz    = 200000000;
    table->periph_clock_hz = 200000000;

    eos_mem_region_t flash_region = {
        .base = C28X_FLASH_BASE,
        .size = C28X_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = C28X_RAM_BASE,
        .size = C28X_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_c28x_get_ops(void)
{
    return &c28x_ops;
}
