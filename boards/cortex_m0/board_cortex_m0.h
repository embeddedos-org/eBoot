// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m0.h
 * @brief STM32F030 Cortex-M0 board configuration
 */

#ifndef BOARD_CORTEX_M0_H
#define BOARD_CORTEX_M0_H

#include "eos_hal.h"

#define CORTEX_M0_FLASH_BASE            0x08000000
#define CORTEX_M0_FLASH_SIZE            (32*1024)
#define CORTEX_M0_RAM_BASE              0x20000000
#define CORTEX_M0_RAM_SIZE              (4*1024)

#define CORTEX_M0_SLOT_A_ADDR           (CORTEX_M0_FLASH_BASE + 0x20000)
#define CORTEX_M0_SLOT_A_SIZE           (CORTEX_M0_FLASH_SIZE / 3)
#define CORTEX_M0_SLOT_B_ADDR           (CORTEX_M0_FLASH_BASE + CORTEX_M0_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M0_SLOT_B_SIZE           (CORTEX_M0_FLASH_SIZE / 3)
#define CORTEX_M0_RECOVERY_ADDR         (CORTEX_M0_FLASH_BASE + 2 * CORTEX_M0_FLASH_SIZE / 3)
#define CORTEX_M0_RECOVERY_SIZE         (CORTEX_M0_FLASH_SIZE / 8)
#define CORTEX_M0_BOOTCTL_ADDR          (CORTEX_M0_FLASH_BASE + CORTEX_M0_FLASH_SIZE - 0x2000)
#define CORTEX_M0_BOOTCTL_BACKUP_ADDR   (CORTEX_M0_FLASH_BASE + CORTEX_M0_FLASH_SIZE - 0x4000)
#define CORTEX_M0_LOG_ADDR              (CORTEX_M0_FLASH_BASE + CORTEX_M0_FLASH_SIZE - 0x6000)

#define CORTEX_M0_CPU_HZ                48000000
#define CORTEX_M0_BOARD_ID              0x0001

void board_cortex_m0_early_init(void);
const eos_board_ops_t *board_cortex_m0_get_ops(void);

#endif /* BOARD_CORTEX_M0_H */
