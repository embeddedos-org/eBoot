// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_powerpc.c
 * @brief PowerPC MPC8540 board port (e500 core, DUART)
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"
/* The map lives in the header only. This file used to carry a second copy of
 * it, under shorter names and without the size constants, and the two had
 * drifted: #197 is the flash size, but the duplication is how it got there. */
#include "board_powerpc.h"

static const eos_board_ops_t powerpc_ops = {
    .flash_base       = POWERPC_FLASH_BASE,
    .flash_size       = POWERPC_FLASH_SIZE,
    .slot_a_addr      = POWERPC_SLOT_A_ADDR,
    .slot_a_size      = POWERPC_SLOT_A_SIZE,
    .slot_b_addr      = POWERPC_SLOT_B_ADDR,
    .slot_b_size      = POWERPC_SLOT_B_SIZE,
    .recovery_addr    = POWERPC_RECOVERY_ADDR,
    .recovery_size    = POWERPC_RECOVERY_SIZE,
    .bootctl_addr     = POWERPC_BOOTCTL_ADDR,
    .bootctl_backup_addr = POWERPC_BOOTCTL_BACKUP_ADDR,
    .log_addr         = POWERPC_LOG_ADDR,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_powerpc_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PowerPC-MPC8540");

    table->board_id = 0x1101;
    table->board_revision = 1;
    table->cpu_clock_hz    = 800000000;
    table->bus_clock_hz    = 800000000;
    table->periph_clock_hz = 33333333;

    eos_mem_region_t flash_region = {
        .base = POWERPC_FLASH_BASE,
        .size = POWERPC_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = POWERPC_RAM_BASE,
        .size = POWERPC_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);

    eos_periph_entry_t uart0 = {
        .type = EOS_PERIPH_UART,
        .instance = 0,
        .base_addr = 0xE0004500,
        .irq_num = 26,
        .clock_hz = 33333333,
        .flags = 0,
    };
    eos_device_table_add_peripheral(table, &uart0);
}

const eos_board_ops_t *board_powerpc_get_ops(void)
{
    return &powerpc_ops;
}
