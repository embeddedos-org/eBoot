// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic16.c
 * @brief PIC16F877A board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define PIC16_FLASH_BASE    0x0000
#define PIC16_FLASH_SIZE    (14*1024)
#define PIC16_RAM_BASE      0x0020
#define PIC16_RAM_SIZE      (368)
/* This port names no slots, no recovery image and no boot-control block
 * (#199). It used to compute them from these formulas, copied across eight
 * ports from one with a megabyte of flash:
 *
 *     SLOT_A   = FLASH_BASE + 0x20000
 *     SLOT_B   = FLASH_BASE + FLASH_SIZE / 3 + 0x20000
 *     RECOVERY = FLASH_BASE + 2 * FLASH_SIZE / 3
 *     BOOTCTL  = FLASH_BASE + FLASH_SIZE - 0x2000
 *
 * The 0x20000 is a fixed 128 KiB bootloader reservation that does not depend
 * on the flash size, so on a device of 14 KiB both slots started past the end
 * of the flash declared right above them: slot A at 0x20000, slot B at
 * 0x212aa, in a device ending at 0x3800.
 *
 * The honest answer for a device this small is that it has no A/B layout to
 * declare. Stage-0 alone occupies 16 KiB on the one board in this tree with
 * real drivers (stm32f4), and 14 KiB cannot hold that plus two slots plus a
 * recovery image plus the boot-control page. The ops table already names no
 * flash operations on this port -- every entry is NULL -- and it now names no
 * regions either; a field left out reads as 0, which eos_board_ops_t
 * documents as "this port has none". Declaring addresses that are not in the
 * device was worse than declaring nothing: docs/board-ops-status.md and the
 * README present the ops table as the port's memory map, so whoever writes
 * the first flash driver here would have inherited them.
 *
 * A port that grows an A/B layout declares it with sizes, as pic24 and
 * cortex_m0plus now do, and tools/board_memory_map.py checks it fits. */

static const eos_board_ops_t pic16_ops = {
    .flash_base       = PIC16_FLASH_BASE,
    .flash_size       = PIC16_FLASH_SIZE,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_pic16_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "PIC16F877A");
    table->board_id = 0xD301;
    table->board_revision = 1;
    table->cpu_clock_hz    = 20000000;
    table->bus_clock_hz    = 20000000;
    table->periph_clock_hz = 20000000;

    eos_mem_region_t flash_region = {
        .base = PIC16_FLASH_BASE,
        .size = PIC16_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = PIC16_RAM_BASE,
        .size = PIC16_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_pic16_get_ops(void)
{
    return &pic16_ops;
}
