// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_hexagon.h
 * @brief Hexagon SDM845 board configuration
 */

#ifndef BOARD_HEXAGON_H
#define BOARD_HEXAGON_H

#include "eos_hal.h"

#define HEXAGON_FLASH_BASE            0x00000000
#define HEXAGON_FLASH_SIZE            (4*1024*1024)
#define HEXAGON_RAM_BASE              0x00000000
#define HEXAGON_RAM_SIZE              (8*1024*1024)

#define HEXAGON_SLOT_A_ADDR           (HEXAGON_FLASH_BASE + 0x20000)
#define HEXAGON_SLOT_A_SIZE           (HEXAGON_FLASH_SIZE / 3)
#define HEXAGON_SLOT_B_ADDR           (HEXAGON_FLASH_BASE + HEXAGON_FLASH_SIZE / 3 + 0x20000)
#define HEXAGON_SLOT_B_SIZE           (HEXAGON_FLASH_SIZE / 3)
#define HEXAGON_RECOVERY_ADDR         (HEXAGON_FLASH_BASE + 2 * HEXAGON_FLASH_SIZE / 3)
#define HEXAGON_RECOVERY_SIZE         (HEXAGON_FLASH_SIZE / 8)
#define HEXAGON_BOOTCTL_ADDR          (HEXAGON_FLASH_BASE + HEXAGON_FLASH_SIZE - 0x2000)
#define HEXAGON_BOOTCTL_BACKUP_ADDR   (HEXAGON_FLASH_BASE + HEXAGON_FLASH_SIZE - 0x4000)
#define HEXAGON_LOG_ADDR              (HEXAGON_FLASH_BASE + HEXAGON_FLASH_SIZE - 0x6000)

#define HEXAGON_CPU_HZ                1500000000
#define HEXAGON_BOARD_ID              0xFB01

void board_hexagon_early_init(void);
const eos_board_ops_t *board_hexagon_get_ops(void);

#endif /* BOARD_HEXAGON_H */
