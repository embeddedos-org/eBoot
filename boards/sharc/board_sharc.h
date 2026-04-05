// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_sharc.h
 * @brief SHARC ADSP-21489 board configuration
 */

#ifndef BOARD_SHARC_H
#define BOARD_SHARC_H

#include "eos_hal.h"

#define SHARC_FLASH_BASE            0x00000000
#define SHARC_FLASH_SIZE            (4*1024*1024)
#define SHARC_RAM_BASE              0x00080000
#define SHARC_RAM_SIZE              (5*1024*1024)

#define SHARC_SLOT_A_ADDR           (SHARC_FLASH_BASE + 0x20000)
#define SHARC_SLOT_A_SIZE           (SHARC_FLASH_SIZE / 3)
#define SHARC_SLOT_B_ADDR           (SHARC_FLASH_BASE + SHARC_FLASH_SIZE / 3 + 0x20000)
#define SHARC_SLOT_B_SIZE           (SHARC_FLASH_SIZE / 3)
#define SHARC_RECOVERY_ADDR         (SHARC_FLASH_BASE + 2 * SHARC_FLASH_SIZE / 3)
#define SHARC_RECOVERY_SIZE         (SHARC_FLASH_SIZE / 8)
#define SHARC_BOOTCTL_ADDR          (SHARC_FLASH_BASE + SHARC_FLASH_SIZE - 0x2000)
#define SHARC_BOOTCTL_BACKUP_ADDR   (SHARC_FLASH_BASE + SHARC_FLASH_SIZE - 0x4000)
#define SHARC_LOG_ADDR              (SHARC_FLASH_BASE + SHARC_FLASH_SIZE - 0x6000)

#define SHARC_CPU_HZ                450000000
#define SHARC_BOARD_ID              0xFA01

void board_sharc_early_init(void);
const eos_board_ops_t *board_sharc_get_ops(void);

#endif /* BOARD_SHARC_H */
