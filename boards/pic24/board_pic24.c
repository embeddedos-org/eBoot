// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic24.c
 * @brief PIC24FJ128GA010 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PIC24_FLASH_BASE    0x000000
#define PIC24_FLASH_SIZE    (128*1024)
#define PIC24_RAM_BASE      0x000800
#define PIC24_RAM_SIZE      (8*1024)
/* Derived from the declared flash size (#199), not from the fixed 0x20000
 * bootloader reservation the eight ports in that issue shared. That constant
 * came from a port with a megabyte of flash; on 128 KiB it put slot A at
 * 0x20000 and slot B at 0x2aaaa, both past the 0x20000 end of the flash
 * declared right above, and the boot-control block at 0x1e000 inside nothing.
 *
 * 128 KiB is enough for an A/B layout, so this port keeps one and states it
 * with sizes, which is what lets tools/board_memory_map.py check that it
 * fits. The first 16 KiB are the bootloader's, the size stage-0 occupies on
 * the one board in this tree with real drivers; the port declares no
 * bootloader region because it has no write-protect primitive to go with one.
 * Slot sizes stay equal, so an image that fits one slot fits the other. The
 * boot-control address is the one the old formula produced, unchanged.
 *
 *   (bootloader)  0x00000 - 0x04000    16 KiB
 *   slot A        0x04000 - 0x0E000    40 KiB
 *   slot B        0x0E000 - 0x18000    40 KiB
 *   recovery      0x18000 - 0x1E000    24 KiB
 *   bootctl       0x1E000 - 0x20000     8 KiB
 */
#define PIC24_BOOTLOADER_SIZE (16*1024)
#define PIC24_SLOT_A_ADDR     (PIC24_FLASH_BASE + PIC24_BOOTLOADER_SIZE)
#define PIC24_SLOT_A_SIZE     (40*1024)
#define PIC24_SLOT_B_ADDR     (PIC24_SLOT_A_ADDR + PIC24_SLOT_A_SIZE)
#define PIC24_SLOT_B_SIZE     (40*1024)
#define PIC24_RECOVERY_ADDR   (PIC24_SLOT_B_ADDR + PIC24_SLOT_B_SIZE)
#define PIC24_RECOVERY_SIZE   (24*1024)
#define PIC24_BOOTCTL_ADDR    (PIC24_FLASH_BASE + PIC24_FLASH_SIZE - 0x2000)

static const eos_board_ops_t pic24_ops = {
    .flash_base       = PIC24_FLASH_BASE,
    .flash_size       = PIC24_FLASH_SIZE,
    .slot_a_addr      = PIC24_SLOT_A_ADDR,
    .slot_a_size      = PIC24_SLOT_A_SIZE,
    .slot_b_addr      = PIC24_SLOT_B_ADDR,
    .slot_b_size      = PIC24_SLOT_B_SIZE,
    .recovery_addr    = PIC24_RECOVERY_ADDR,
    .recovery_size    = PIC24_RECOVERY_SIZE,
    .bootctl_addr     = PIC24_BOOTCTL_ADDR,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pic24_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PIC24FJ128GA010");
    table->board_id = 0xD501;
    table->board_revision = 1;
    table->cpu_clock_hz    = 32000000;
    table->bus_clock_hz    = 32000000;
    table->periph_clock_hz = 32000000;

    eos_mem_region_t flash_region = {
        .base = PIC24_FLASH_BASE,
        .size = PIC24_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PIC24_RAM_BASE,
        .size = PIC24_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pic24_get_ops(void)
{
    return &pic24_ops;
}
