// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_c166.h
 * @brief XC2267M C166 board configuration
 */

#ifndef BOARD_C166_H
#define BOARD_C166_H

#include "eos_hal.h"

#define C166_FLASH_BASE            0x000000
#define C166_FLASH_SIZE            (768*1024)
#define C166_RAM_BASE              0x00F000
#define C166_RAM_SIZE              (48*1024)

#define C166_SLOT_A_ADDR           (C166_FLASH_BASE + 0x20000)
#define C166_SLOT_A_SIZE           (C166_FLASH_SIZE / 3)
#define C166_SLOT_B_ADDR           (C166_FLASH_BASE + C166_FLASH_SIZE / 3 + 0x20000)
#define C166_SLOT_B_SIZE           (C166_FLASH_SIZE / 3)
#define C166_RECOVERY_ADDR         (C166_FLASH_BASE + 2 * C166_FLASH_SIZE / 3)
#define C166_RECOVERY_SIZE         (C166_FLASH_SIZE / 8)
#define C166_BOOTCTL_ADDR          (C166_FLASH_BASE + C166_FLASH_SIZE - 0x2000)
#define C166_BOOTCTL_BACKUP_ADDR   (C166_FLASH_BASE + C166_FLASH_SIZE - 0x4000)
#define C166_LOG_ADDR              (C166_FLASH_BASE + C166_FLASH_SIZE - 0x6000)

#define C166_CPU_HZ                80000000
#define C166_BOARD_ID              0xF401

void board_c166_early_init(void);
const eos_board_ops_t *board_c166_get_ops(void);

#endif /* BOARD_C166_H */
