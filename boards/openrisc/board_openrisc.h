// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_openrisc.h
 * @brief OpenRISC mor1kx board configuration
 */

#ifndef BOARD_OPENRISC_H
#define BOARD_OPENRISC_H

#include "eos_hal.h"

#define OPENRISC_FLASH_BASE            0x00000000
#define OPENRISC_FLASH_SIZE            (4*1024*1024)
#define OPENRISC_RAM_BASE              0x00000000
#define OPENRISC_RAM_SIZE              (16*1024*1024)

#define OPENRISC_SLOT_A_ADDR           (OPENRISC_FLASH_BASE + 0x20000)
#define OPENRISC_SLOT_A_SIZE           (OPENRISC_FLASH_SIZE / 3)
#define OPENRISC_SLOT_B_ADDR           (OPENRISC_FLASH_BASE + OPENRISC_FLASH_SIZE / 3 + 0x20000)
#define OPENRISC_SLOT_B_SIZE           (OPENRISC_FLASH_SIZE / 3)
#define OPENRISC_RECOVERY_ADDR         (OPENRISC_FLASH_BASE + 2 * OPENRISC_FLASH_SIZE / 3)
#define OPENRISC_RECOVERY_SIZE         (OPENRISC_FLASH_SIZE / 8)
#define OPENRISC_BOOTCTL_ADDR          (OPENRISC_FLASH_BASE + OPENRISC_FLASH_SIZE - 0x2000)
#define OPENRISC_BOOTCTL_BACKUP_ADDR   (OPENRISC_FLASH_BASE + OPENRISC_FLASH_SIZE - 0x4000)
#define OPENRISC_LOG_ADDR              (OPENRISC_FLASH_BASE + OPENRISC_FLASH_SIZE - 0x6000)

#define OPENRISC_CPU_HZ                100000000
#define OPENRISC_BOARD_ID              0xF701

void board_openrisc_early_init(void);
const eos_board_ops_t *board_openrisc_get_ops(void);

#endif /* BOARD_OPENRISC_H */
