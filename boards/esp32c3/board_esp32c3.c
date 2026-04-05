// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_esp32c3.c
 * @brief ESP32-C3 RISC-V board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define ESP32C3_FLASH_BASE    0x00000000
#define ESP32C3_FLASH_SIZE    (4*1024*1024)
#define ESP32C3_RAM_BASE      0x3FC80000
#define ESP32C3_RAM_SIZE      (400*1024)
#define ESP32C3_SLOT_A        (ESP32C3_FLASH_BASE + 0x20000)
#define ESP32C3_SLOT_B        (ESP32C3_FLASH_BASE + ESP32C3_FLASH_SIZE / 3 + 0x20000)
#define ESP32C3_RECOVERY      (ESP32C3_FLASH_BASE + 2 * ESP32C3_FLASH_SIZE / 3)
#define ESP32C3_BOOTCTL       (ESP32C3_FLASH_BASE + ESP32C3_FLASH_SIZE - 0x2000)

static const eos_board_ops_t esp32c3_ops = {
    .flash_base       = ESP32C3_FLASH_BASE,
    .flash_size       = ESP32C3_FLASH_SIZE,
    .slot_a_addr      = ESP32C3_SLOT_A,
    .slot_b_addr      = ESP32C3_SLOT_B,
    .recovery_addr    = ESP32C3_RECOVERY,
    .bootctl_addr     = ESP32C3_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_esp32c3_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "ESP32-C3 RISC-V");
    table->board_id = 0x0301;
    table->board_revision = 1;
    table->cpu_clock_hz    = 160000000;
    table->bus_clock_hz    = 160000000;
    table->periph_clock_hz = 160000000;

    eos_mem_region_t flash_region = {
        .base = ESP32C3_FLASH_BASE,
        .size = ESP32C3_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = ESP32C3_RAM_BASE,
        .size = ESP32C3_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_esp32c3_get_ops(void)
{
    return &esp32c3_ops;
}
