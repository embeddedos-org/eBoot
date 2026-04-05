// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_ceva.h
 * @brief CEVA-XM6 board configuration
 */

#ifndef BOARD_CEVA_H
#define BOARD_CEVA_H

#include "eos_hal.h"

#define CEVA_FLASH_BASE            0x00000000
#define CEVA_FLASH_SIZE            (4*1024*1024)
#define CEVA_RAM_BASE              0x00000000
#define CEVA_RAM_SIZE              (4*1024*1024)

#define CEVA_SLOT_A_ADDR           (CEVA_FLASH_BASE + 0x20000)
#define CEVA_SLOT_A_SIZE           (CEVA_FLASH_SIZE / 3)
#define CEVA_SLOT_B_ADDR           (CEVA_FLASH_BASE + CEVA_FLASH_SIZE / 3 + 0x20000)
#define CEVA_SLOT_B_SIZE           (CEVA_FLASH_SIZE / 3)
#define CEVA_RECOVERY_ADDR         (CEVA_FLASH_BASE + 2 * CEVA_FLASH_SIZE / 3)
#define CEVA_RECOVERY_SIZE         (CEVA_FLASH_SIZE / 8)
#define CEVA_BOOTCTL_ADDR          (CEVA_FLASH_BASE + CEVA_FLASH_SIZE - 0x2000)
#define CEVA_BOOTCTL_BACKUP_ADDR   (CEVA_FLASH_BASE + CEVA_FLASH_SIZE - 0x4000)
#define CEVA_LOG_ADDR              (CEVA_FLASH_BASE + CEVA_FLASH_SIZE - 0x6000)

#define CEVA_CPU_HZ                1000000000
#define CEVA_BOARD_ID              0xFC01

void board_ceva_early_init(void);
const eos_board_ops_t *board_ceva_get_ops(void);

#endif /* BOARD_CEVA_H */
