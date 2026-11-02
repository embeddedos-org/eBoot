// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_stm32f4.h
 * @brief STM32F4 reference board configuration
 */

#ifndef BOARD_STM32F4_H
#define BOARD_STM32F4_H

#include "eos_hal.h"

/*
 * STM32F407 Flash Memory Map (1MB total)
 *
 * The F407 erases whole sectors only, and they are not uniform:
 * sectors 0-3 are 16K, sector 4 is 64K, sectors 5-11 are 128K. Every
 * region therefore owns whole sectors -- erasing one region (a slot
 * update, a boot control save, a log clear) must never erase another.
 * Slot B, the recovery image, both boot control copies and the boot log
 * used to share sector 11, so saving the boot control block wiped the
 * other four.
 *
 * Sector  Address range             Size   Region
 * 0       0x08000000 - 0x08003FFF   16K    Stage-0 (eBootloader)
 * 1-3     0x08004000 - 0x0800FFFF   48K    Stage-1 (E-Boot)
 * 4       0x08010000 - 0x0801FFFF   64K    Boot log
 * 5-6     0x08020000 - 0x0805FFFF   256K   Slot A (application firmware)
 * 7-8     0x08060000 - 0x0809FFFF   256K   Slot B (candidate firmware)
 * 9       0x080A0000 - 0x080BFFFF   128K   Recovery image (golden)
 * 10      0x080C0000 - 0x080DFFFF   128K   Boot control (primary)
 * 11      0x080E0000 - 0x080FFFFF   128K   Boot control (backup)
 *
 * Stage-0 and stage-1 match stm32f4_stage0.ld and stm32f4_stage1.ld.
 */

#define STM32F4_FLASH_BASE          0x08000000
#define STM32F4_FLASH_SIZE          (1024 * 1024)  /* 1MB */

#define STM32F4_STAGE0_ADDR         0x08000000
#define STM32F4_STAGE0_SIZE         (16 * 1024)

#define STM32F4_STAGE1_ADDR         0x08004000
#define STM32F4_STAGE1_SIZE         (48 * 1024)

#define STM32F4_LOG_ADDR            0x08010000
#define STM32F4_LOG_SIZE            (64 * 1024)

#define STM32F4_SLOT_A_ADDR         0x08020000
#define STM32F4_SLOT_A_SIZE         (256 * 1024)

#define STM32F4_SLOT_B_ADDR         0x08060000
#define STM32F4_SLOT_B_SIZE         (256 * 1024)

#define STM32F4_RECOVERY_ADDR       0x080A0000
#define STM32F4_RECOVERY_SIZE       (128 * 1024)

#define STM32F4_BOOTCTL_ADDR        0x080C0000
#define STM32F4_BOOTCTL_SIZE        (128 * 1024)

#define STM32F4_BOOTCTL_BACKUP_ADDR 0x080E0000
#define STM32F4_BOOTCTL_BACKUP_SIZE (128 * 1024)

#define STM32F4_VECTOR_OFFSET       (STM32F4_STAGE1_ADDR - STM32F4_FLASH_BASE)

/* ---- Flash sector geometry ---- */

#define STM32F4_FLASH_SECTORS       12

/**
 * @brief Sector number holding @p addr, or -1 outside the 1MB array.
 */
static inline int stm32f4_flash_sector_of(uint32_t addr)
{
    if (addr < STM32F4_FLASH_BASE)
        return -1;
    uint32_t off = addr - STM32F4_FLASH_BASE;
    if (off < 0x10000)                 /* sectors 0-3: 16K each */
        return (int)(off / 0x4000);
    if (off < 0x20000)                 /* sector 4: 64K */
        return 4;
    if (off < STM32F4_FLASH_SIZE)      /* sectors 5-11: 128K each */
        return 4 + (int)(off / 0x20000);
    return -1;
}

/**
 * @brief First and last sector an erase of [addr, addr + len) must clear.
 *
 * Every sector the range touches is included. A sector is the smallest
 * unit the F407 can erase, so the erase may clear bytes past either end of
 * the range; the memory map above keeps each region sector-aligned so
 * those bytes never belong to another region.
 *
 * @return 0 on success; -1 for len == 0, or a range that wraps or leaves
 *         the flash array (nothing is to be erased then).
 */
static inline int stm32f4_flash_sector_range(uint32_t addr, size_t len,
                                             int *first, int *last)
{
    if (len == 0 || (uint64_t)addr + len > (uint64_t)STM32F4_FLASH_BASE +
                                            STM32F4_FLASH_SIZE)
        return -1;
    int f = stm32f4_flash_sector_of(addr);
    int l = stm32f4_flash_sector_of((uint32_t)(addr + len - 1));
    if (f < 0 || l < 0)
        return -1;
    *first = f;
    *last  = l;
    return 0;
}

/* UART for recovery */
#define STM32F4_UART_INSTANCE       2  /* USART2 on PA2/PA3 */

/* Recovery GPIO */
#define STM32F4_RECOVERY_PORT       'C'
#define STM32F4_RECOVERY_PIN        13  /* PC13 — user button on Nucleo */

/* Board entry points */
void board_early_init(void);
const eos_board_ops_t *board_get_ops(void);

#endif /* BOARD_STM32F4_H */
