// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_parisc.h
 * @brief PA-8700 board configuration
 */

#ifndef BOARD_PARISC_H
#define BOARD_PARISC_H

#include "eos_hal.h"

#define PARISC_FLASH_BASE            0x00000000
#define PARISC_FLASH_SIZE            (4*1024*1024)
#define PARISC_RAM_BASE              0x00000000
#define PARISC_RAM_SIZE              (4096UL*1024*1024)

#define PARISC_SLOT_A_ADDR           (PARISC_FLASH_BASE + 0x20000)
#define PARISC_SLOT_A_SIZE           (PARISC_FLASH_SIZE / 3)
#define PARISC_SLOT_B_ADDR           (PARISC_FLASH_BASE + PARISC_FLASH_SIZE / 3 + 0x20000)
#define PARISC_SLOT_B_SIZE           (PARISC_FLASH_SIZE / 3)
#define PARISC_RECOVERY_ADDR         (PARISC_FLASH_BASE + 2 * PARISC_FLASH_SIZE / 3)
#define PARISC_RECOVERY_SIZE         (PARISC_FLASH_SIZE / 8)
#define PARISC_BOOTCTL_ADDR          (PARISC_FLASH_BASE + PARISC_FLASH_SIZE - 0x2000)
#define PARISC_BOOTCTL_BACKUP_ADDR   (PARISC_FLASH_BASE + PARISC_FLASH_SIZE - 0x4000)
#define PARISC_LOG_ADDR              (PARISC_FLASH_BASE + PARISC_FLASH_SIZE - 0x6000)

#define PARISC_CPU_HZ                875000000
#define PARISC_BOARD_ID              0x0801

void board_parisc_early_init(void);
const eos_board_ops_t *board_parisc_get_ops(void);

#endif /* BOARD_PARISC_H */
