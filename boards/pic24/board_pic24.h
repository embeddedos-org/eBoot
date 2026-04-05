// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic24.h
 * @brief PIC24FJ128GA010 board configuration
 */

#ifndef BOARD_PIC24_H
#define BOARD_PIC24_H

#include "eos_hal.h"

#define PIC24_FLASH_BASE            0x000000
#define PIC24_FLASH_SIZE            (128*1024)
#define PIC24_RAM_BASE              0x000800
#define PIC24_RAM_SIZE              (8*1024)

#define PIC24_SLOT_A_ADDR           (PIC24_FLASH_BASE + 0x20000)
#define PIC24_SLOT_A_SIZE           (PIC24_FLASH_SIZE / 3)
#define PIC24_SLOT_B_ADDR           (PIC24_FLASH_BASE + PIC24_FLASH_SIZE / 3 + 0x20000)
#define PIC24_SLOT_B_SIZE           (PIC24_FLASH_SIZE / 3)
#define PIC24_RECOVERY_ADDR         (PIC24_FLASH_BASE + 2 * PIC24_FLASH_SIZE / 3)
#define PIC24_RECOVERY_SIZE         (PIC24_FLASH_SIZE / 8)
#define PIC24_BOOTCTL_ADDR          (PIC24_FLASH_BASE + PIC24_FLASH_SIZE - 0x2000)
#define PIC24_BOOTCTL_BACKUP_ADDR   (PIC24_FLASH_BASE + PIC24_FLASH_SIZE - 0x4000)
#define PIC24_LOG_ADDR              (PIC24_FLASH_BASE + PIC24_FLASH_SIZE - 0x6000)

#define PIC24_CPU_HZ                32000000
#define PIC24_BOARD_ID              0xD501

void board_pic24_early_init(void);
const eos_board_ops_t *board_pic24_get_ops(void);

#endif /* BOARD_PIC24_H */
