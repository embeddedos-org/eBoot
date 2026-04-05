// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_rl78.h
 * @brief RL78/G14 board configuration
 */

#ifndef BOARD_RL78_H
#define BOARD_RL78_H

#include "eos_hal.h"

#define RL78_FLASH_BASE            0x00000
#define RL78_FLASH_SIZE            (512*1024)
#define RL78_RAM_BASE              0xFF700
#define RL78_RAM_SIZE              (48*1024)

#define RL78_SLOT_A_ADDR           (RL78_FLASH_BASE + 0x20000)
#define RL78_SLOT_A_SIZE           (RL78_FLASH_SIZE / 3)
#define RL78_SLOT_B_ADDR           (RL78_FLASH_BASE + RL78_FLASH_SIZE / 3 + 0x20000)
#define RL78_SLOT_B_SIZE           (RL78_FLASH_SIZE / 3)
#define RL78_RECOVERY_ADDR         (RL78_FLASH_BASE + 2 * RL78_FLASH_SIZE / 3)
#define RL78_RECOVERY_SIZE         (RL78_FLASH_SIZE / 8)
#define RL78_BOOTCTL_ADDR          (RL78_FLASH_BASE + RL78_FLASH_SIZE - 0x2000)
#define RL78_BOOTCTL_BACKUP_ADDR   (RL78_FLASH_BASE + RL78_FLASH_SIZE - 0x4000)
#define RL78_LOG_ADDR              (RL78_FLASH_BASE + RL78_FLASH_SIZE - 0x6000)

#define RL78_CPU_HZ                32000000
#define RL78_BOARD_ID              0xF101

void board_rl78_early_init(void);
const eos_board_ops_t *board_rl78_get_ops(void);

#endif /* BOARD_RL78_H */
