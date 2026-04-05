// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic18.h
 * @brief PIC18F4550 board configuration
 */

#ifndef BOARD_PIC18_H
#define BOARD_PIC18_H

#include "eos_hal.h"

#define PIC18_FLASH_BASE            0x0000
#define PIC18_FLASH_SIZE            (32*1024)
#define PIC18_RAM_BASE              0x0080
#define PIC18_RAM_SIZE              (2*1024)

#define PIC18_SLOT_A_ADDR           (PIC18_FLASH_BASE + 0x20000)
#define PIC18_SLOT_A_SIZE           (PIC18_FLASH_SIZE / 3)
#define PIC18_SLOT_B_ADDR           (PIC18_FLASH_BASE + PIC18_FLASH_SIZE / 3 + 0x20000)
#define PIC18_SLOT_B_SIZE           (PIC18_FLASH_SIZE / 3)
#define PIC18_RECOVERY_ADDR         (PIC18_FLASH_BASE + 2 * PIC18_FLASH_SIZE / 3)
#define PIC18_RECOVERY_SIZE         (PIC18_FLASH_SIZE / 8)
#define PIC18_BOOTCTL_ADDR          (PIC18_FLASH_BASE + PIC18_FLASH_SIZE - 0x2000)
#define PIC18_BOOTCTL_BACKUP_ADDR   (PIC18_FLASH_BASE + PIC18_FLASH_SIZE - 0x4000)
#define PIC18_LOG_ADDR              (PIC18_FLASH_BASE + PIC18_FLASH_SIZE - 0x6000)

#define PIC18_CPU_HZ                48000000
#define PIC18_BOARD_ID              0xD401

void board_pic18_early_init(void);
const eos_board_ops_t *board_pic18_get_ops(void);

#endif /* BOARD_PIC18_H */
