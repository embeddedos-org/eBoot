// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_xtensa_hifi.c
 * @brief Xtensa HiFi5 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define XTENSA_HIFI_FLASH_BASE    0x00000000
#define XTENSA_HIFI_FLASH_SIZE    (4*1024*1024)
#define XTENSA_HIFI_RAM_BASE      0x00000000
#define XTENSA_HIFI_RAM_SIZE      (4*1024*1024)
#define XTENSA_HIFI_SLOT_A        (XTENSA_HIFI_FLASH_BASE + 0x20000)
#define XTENSA_HIFI_SLOT_B        (XTENSA_HIFI_FLASH_BASE + XTENSA_HIFI_FLASH_SIZE / 3 + 0x20000)
#define XTENSA_HIFI_RECOVERY      (XTENSA_HIFI_FLASH_BASE + 2 * XTENSA_HIFI_FLASH_SIZE / 3)
#define XTENSA_HIFI_BOOTCTL       (XTENSA_HIFI_FLASH_BASE + XTENSA_HIFI_FLASH_SIZE - 0x2000)

static const eos_board_ops_t xtensa_hifi_ops = {
    .flash_base       = XTENSA_HIFI_FLASH_BASE,
    .flash_size       = XTENSA_HIFI_FLASH_SIZE,
    .slot_a_addr      = XTENSA_HIFI_SLOT_A,
    .slot_b_addr      = XTENSA_HIFI_SLOT_B,
    .recovery_addr    = XTENSA_HIFI_RECOVERY,
    .bootctl_addr     = XTENSA_HIFI_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_xtensa_hifi_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "Xtensa HiFi5");
    table->board_id = 0xFD01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 800000000;
    table->bus_clock_hz    = 800000000;
    table->periph_clock_hz = 800000000;

    eos_mem_region_t flash_region = {
        .base = XTENSA_HIFI_FLASH_BASE,
        .size = XTENSA_HIFI_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = XTENSA_HIFI_RAM_BASE,
        .size = XTENSA_HIFI_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_xtensa_hifi_get_ops(void)
{
    return &xtensa_hifi_ops;
}
