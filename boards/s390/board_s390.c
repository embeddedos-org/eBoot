// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_s390.c
 * @brief IBM z15 board port
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define S390_FLASH_BASE    0x00000000
#define S390_FLASH_SIZE    (4*1024*1024)
#define S390_RAM_BASE      0x00000000
#define S390_RAM_SIZE      (524288UL*1024*1024)
#define S390_SLOT_A        (S390_FLASH_BASE + 0x20000)
#define S390_SLOT_B        (S390_FLASH_BASE + S390_FLASH_SIZE / 3 + 0x20000)
#define S390_RECOVERY      (S390_FLASH_BASE + 2 * S390_FLASH_SIZE / 3)
#define S390_BOOTCTL       (S390_FLASH_BASE + S390_FLASH_SIZE - 0x2000)

static const eos_board_ops_t s390_ops = {
    .flash_base       = S390_FLASH_BASE,
    .flash_size       = S390_FLASH_SIZE,
    .slot_a_addr      = S390_SLOT_A,
    .slot_b_addr      = S390_SLOT_B,
    .recovery_addr    = S390_RECOVERY,
    .bootctl_addr     = S390_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = NULL,
    .flash_write      = NULL,
    .flash_erase      = NULL,
    .jump             = NULL,
};

void board_s390_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "IBM z15");
    table->board_id = 0x0B01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 5200000000;
    table->bus_clock_hz    = 5200000000;
    table->periph_clock_hz = 5200000000;

    eos_mem_region_t flash_region = {
        .base = S390_FLASH_BASE,
        .size = S390_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = S390_RAM_BASE,
        .size = S390_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);
}

const eos_board_ops_t *board_s390_get_ops(void)
{
    return &s390_ops;
}
