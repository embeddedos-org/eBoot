// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_blackfin.c
 * @brief Blackfin ADSP-BF537 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define BLACKFIN_FLASH_BASE    0x20000000
#define BLACKFIN_FLASH_SIZE    (4*1024*1024)
#define BLACKFIN_RAM_BASE      0x00000000
#define BLACKFIN_RAM_SIZE      (128*1024*1024)
#define BLACKFIN_SLOT_A        (BLACKFIN_FLASH_BASE + 0x20000)
#define BLACKFIN_SLOT_B        (BLACKFIN_FLASH_BASE + BLACKFIN_FLASH_SIZE / 3 + 0x20000)
#define BLACKFIN_RECOVERY      (BLACKFIN_FLASH_BASE + 2 * BLACKFIN_FLASH_SIZE / 3)
#define BLACKFIN_BOOTCTL       (BLACKFIN_FLASH_BASE + BLACKFIN_FLASH_SIZE - 0x2000)

static const eos_board_ops_t blackfin_ops = {
    .flash_base       = BLACKFIN_FLASH_BASE,
    .flash_size       = BLACKFIN_FLASH_SIZE,
    .slot_a_addr      = BLACKFIN_SLOT_A,
    .slot_b_addr      = BLACKFIN_SLOT_B,
    .recovery_addr    = BLACKFIN_RECOVERY,
    .bootctl_addr     = BLACKFIN_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_blackfin_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Blackfin ADSP-BF537");
    table->board_id = 0xF901;
    table->board_revision = 1;
    table->cpu_clock_hz    = 600000000;
    table->bus_clock_hz    = 600000000;
    table->periph_clock_hz = 600000000;

    eos_mem_region_t flash_region = {
        .base = BLACKFIN_FLASH_BASE,
        .size = BLACKFIN_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = BLACKFIN_RAM_BASE,
        .size = BLACKFIN_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_blackfin_get_ops(void)
{
    return &blackfin_ops;
}
