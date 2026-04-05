// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_8051.h
 * @brief 8051 STC89C52 board configuration
 */

#ifndef BOARD_MCS8051_H
#define BOARD_MCS8051_H

#include "eos_hal.h"

#define MCS8051_FLASH_BASE            0x0000
#define MCS8051_FLASH_SIZE            (8*1024)
#define MCS8051_RAM_BASE              0x0000
#define MCS8051_RAM_SIZE              (256)

#define MCS8051_SLOT_A_ADDR           (MCS8051_FLASH_BASE + 0x20000)
#define MCS8051_SLOT_A_SIZE           (MCS8051_FLASH_SIZE / 3)
#define MCS8051_SLOT_B_ADDR           (MCS8051_FLASH_BASE + MCS8051_FLASH_SIZE / 3 + 0x20000)
#define MCS8051_SLOT_B_SIZE           (MCS8051_FLASH_SIZE / 3)
#define MCS8051_RECOVERY_ADDR         (MCS8051_FLASH_BASE + 2 * MCS8051_FLASH_SIZE / 3)
#define MCS8051_RECOVERY_SIZE         (MCS8051_FLASH_SIZE / 8)
#define MCS8051_BOOTCTL_ADDR          (MCS8051_FLASH_BASE + MCS8051_FLASH_SIZE - 0x2000)
#define MCS8051_BOOTCTL_BACKUP_ADDR   (MCS8051_FLASH_BASE + MCS8051_FLASH_SIZE - 0x4000)
#define MCS8051_LOG_ADDR              (MCS8051_FLASH_BASE + MCS8051_FLASH_SIZE - 0x6000)

#define MCS8051_CPU_HZ                24000000
#define MCS8051_BOARD_ID              0xFF01

void board_8051_early_init(void);
const eos_board_ops_t *board_8051_get_ops(void);

#endif /* BOARD_MCS8051_H */
