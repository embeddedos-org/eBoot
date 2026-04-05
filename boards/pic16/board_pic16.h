// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic16.h
 * @brief PIC16F877A board configuration
 */

#ifndef BOARD_PIC16_H
#define BOARD_PIC16_H

#include "eos_hal.h"

#define PIC16_FLASH_BASE            0x0000
#define PIC16_FLASH_SIZE            (14*1024)
#define PIC16_RAM_BASE              0x0020
#define PIC16_RAM_SIZE              (368)

#define PIC16_SLOT_A_ADDR           (PIC16_FLASH_BASE + 0x20000)
#define PIC16_SLOT_A_SIZE           (PIC16_FLASH_SIZE / 3)
#define PIC16_SLOT_B_ADDR           (PIC16_FLASH_BASE + PIC16_FLASH_SIZE / 3 + 0x20000)
#define PIC16_SLOT_B_SIZE           (PIC16_FLASH_SIZE / 3)
#define PIC16_RECOVERY_ADDR         (PIC16_FLASH_BASE + 2 * PIC16_FLASH_SIZE / 3)
#define PIC16_RECOVERY_SIZE         (PIC16_FLASH_SIZE / 8)
#define PIC16_BOOTCTL_ADDR          (PIC16_FLASH_BASE + PIC16_FLASH_SIZE - 0x2000)
#define PIC16_BOOTCTL_BACKUP_ADDR   (PIC16_FLASH_BASE + PIC16_FLASH_SIZE - 0x4000)
#define PIC16_LOG_ADDR              (PIC16_FLASH_BASE + PIC16_FLASH_SIZE - 0x6000)

#define PIC16_CPU_HZ                20000000
#define PIC16_BOARD_ID              0xD301

void board_pic16_early_init(void);
const eos_board_ops_t *board_pic16_get_ops(void);

#endif /* BOARD_PIC16_H */
