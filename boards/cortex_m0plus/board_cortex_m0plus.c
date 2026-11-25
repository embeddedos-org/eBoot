// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m0plus.c
 * @brief STM32L072 Cortex-M0+ board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define CORTEX_M0PLUS_FLASH_BASE    0x08000000
#define CORTEX_M0PLUS_FLASH_SIZE    (192*1024)
#define CORTEX_M0PLUS_RAM_BASE      0x20000000
#define CORTEX_M0PLUS_RAM_SIZE      (20*1024)
/* Derived from the declared flash size (#199), not from the fixed 0x20000
 * bootloader reservation the eight ports in that issue shared. That constant
 * came from a port with a megabyte of flash; on 192 KiB it put slot B at
 * 0x8030000, exactly the end of the declared flash, and made the recovery
 * address (base + 2 * size / 3) evaluate to 0x8020000 -- the same address as
 * slot A, so the recovery image and slot A were the same bytes.
 *
 * 192 KiB is enough for an A/B layout, so this port keeps one and states it
 * with sizes, which is what lets tools/board_memory_map.py check that it
 * fits. The first 16 KiB are the bootloader's, the size stage-0 occupies on
 * the one board in this tree with real drivers; the port declares no
 * bootloader region because it has no write-protect primitive to go with one.
 * Slot sizes stay equal, so an image that fits one slot fits the other. The
 * boot-control address is the one the old formula produced, unchanged.
 *
 *   (bootloader)  0x08000000 - 0x08004000    16 KiB
 *   slot A        0x08004000 - 0x08014000    64 KiB
 *   slot B        0x08014000 - 0x08024000    64 KiB
 *   recovery      0x08024000 - 0x0802E000    40 KiB
 *   bootctl       0x0802E000 - 0x08030000     8 KiB
 */
#define CORTEX_M0PLUS_BOOTLOADER_SIZE (16*1024)
#define CORTEX_M0PLUS_SLOT_A_ADDR     (CORTEX_M0PLUS_FLASH_BASE + CORTEX_M0PLUS_BOOTLOADER_SIZE)
#define CORTEX_M0PLUS_SLOT_A_SIZE     (64*1024)
#define CORTEX_M0PLUS_SLOT_B_ADDR     (CORTEX_M0PLUS_SLOT_A_ADDR + CORTEX_M0PLUS_SLOT_A_SIZE)
#define CORTEX_M0PLUS_SLOT_B_SIZE     (64*1024)
#define CORTEX_M0PLUS_RECOVERY_ADDR   (CORTEX_M0PLUS_SLOT_B_ADDR + CORTEX_M0PLUS_SLOT_B_SIZE)
#define CORTEX_M0PLUS_RECOVERY_SIZE   (40*1024)
#define CORTEX_M0PLUS_BOOTCTL_ADDR    (CORTEX_M0PLUS_FLASH_BASE + CORTEX_M0PLUS_FLASH_SIZE - 0x2000)

static const eos_board_ops_t cortex_m0plus_ops = {
    .flash_base       = CORTEX_M0PLUS_FLASH_BASE,
    .flash_size       = CORTEX_M0PLUS_FLASH_SIZE,
    .slot_a_addr      = CORTEX_M0PLUS_SLOT_A_ADDR,
    .slot_a_size      = CORTEX_M0PLUS_SLOT_A_SIZE,
    .slot_b_addr      = CORTEX_M0PLUS_SLOT_B_ADDR,
    .slot_b_size      = CORTEX_M0PLUS_SLOT_B_SIZE,
    .recovery_addr    = CORTEX_M0PLUS_RECOVERY_ADDR,
    .recovery_size    = CORTEX_M0PLUS_RECOVERY_SIZE,
    .bootctl_addr     = CORTEX_M0PLUS_BOOTCTL_ADDR,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_cortex_m0plus_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "STM32L072 Cortex-M0+");
    table->board_id = 0x00A1;
    table->board_revision = 1;
    table->cpu_clock_hz    = 32000000;
    table->bus_clock_hz    = 32000000;
    table->periph_clock_hz = 32000000;

    eos_mem_region_t flash_region = {
        .base = CORTEX_M0PLUS_FLASH_BASE,
        .size = CORTEX_M0PLUS_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = CORTEX_M0PLUS_RAM_BASE,
        .size = CORTEX_M0PLUS_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_cortex_m0plus_get_ops(void)
{
    return &cortex_m0plus_ops;
}
