// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pic32.h
 * @brief PIC32MX795F512L board configuration
 */

#ifndef BOARD_PIC32_H
#define BOARD_PIC32_H

#include "eos_hal.h"

#define PIC32_FLASH_BASE            0x9D000000
#define PIC32_FLASH_SIZE            (512*1024)
#define PIC32_RAM_BASE              0xA0000000
#define PIC32_RAM_SIZE              (128*1024)

#define PIC32_SLOT_A_ADDR           (PIC32_FLASH_BASE + 0x20000)
#define PIC32_SLOT_A_SIZE           (PIC32_FLASH_SIZE / 3)
#define PIC32_SLOT_B_ADDR           (PIC32_FLASH_BASE + PIC32_FLASH_SIZE / 3 + 0x20000)
#define PIC32_SLOT_B_SIZE           (PIC32_FLASH_SIZE / 3)
#define PIC32_RECOVERY_ADDR         (PIC32_FLASH_BASE + 2 * PIC32_FLASH_SIZE / 3)
#define PIC32_RECOVERY_SIZE         (PIC32_FLASH_SIZE / 8)
#define PIC32_BOOTCTL_ADDR          (PIC32_FLASH_BASE + PIC32_FLASH_SIZE - 0x2000)
#define PIC32_BOOTCTL_BACKUP_ADDR   (PIC32_FLASH_BASE + PIC32_FLASH_SIZE - 0x4000)
#define PIC32_LOG_ADDR              (PIC32_FLASH_BASE + PIC32_FLASH_SIZE - 0x6000)

#define PIC32_CPU_HZ                80000000
#define PIC32_BOARD_ID              0xD701

void board_pic32_early_init(void);
const eos_board_ops_t *board_pic32_get_ops(void);

#endif /* BOARD_PIC32_H */
