// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_c6000.h
 * @brief TMS320C6748 C6000 board configuration
 */

#ifndef BOARD_C6000_H
#define BOARD_C6000_H

#include "eos_hal.h"

#define C6000_FLASH_BASE            0x00000000
#define C6000_FLASH_SIZE            (4*1024*1024)
#define C6000_RAM_BASE              0xC0000000
#define C6000_RAM_SIZE              (256*1024*1024)

#define C6000_SLOT_A_ADDR           (C6000_FLASH_BASE + 0x20000)
#define C6000_SLOT_A_SIZE           (C6000_FLASH_SIZE / 3)
#define C6000_SLOT_B_ADDR           (C6000_FLASH_BASE + C6000_FLASH_SIZE / 3 + 0x20000)
#define C6000_SLOT_B_SIZE           (C6000_FLASH_SIZE / 3)
#define C6000_RECOVERY_ADDR         (C6000_FLASH_BASE + 2 * C6000_FLASH_SIZE / 3)
#define C6000_RECOVERY_SIZE         (C6000_FLASH_SIZE / 8)
#define C6000_BOOTCTL_ADDR          (C6000_FLASH_BASE + C6000_FLASH_SIZE - 0x2000)
#define C6000_BOOTCTL_BACKUP_ADDR   (C6000_FLASH_BASE + C6000_FLASH_SIZE - 0x4000)
#define C6000_LOG_ADDR              (C6000_FLASH_BASE + C6000_FLASH_SIZE - 0x6000)

#define C6000_CPU_HZ                456000000
#define C6000_BOARD_ID              0xE301

void board_c6000_early_init(void);
const eos_board_ops_t *board_c6000_get_ops(void);

#endif /* BOARD_C6000_H */
