// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m33.h
 * @brief STM32L562 Cortex-M33 board configuration
 */

#ifndef BOARD_CORTEX_M33_H
#define BOARD_CORTEX_M33_H

#include "eos_hal.h"

#define CORTEX_M33_FLASH_BASE            0x08000000
#define CORTEX_M33_FLASH_SIZE            (512*1024)
#define CORTEX_M33_RAM_BASE              0x20000000
#define CORTEX_M33_RAM_SIZE              (256*1024)

#define CORTEX_M33_SLOT_A_ADDR           (CORTEX_M33_FLASH_BASE + 0x20000)
#define CORTEX_M33_SLOT_A_SIZE           (CORTEX_M33_FLASH_SIZE / 3)
#define CORTEX_M33_SLOT_B_ADDR           (CORTEX_M33_FLASH_BASE + CORTEX_M33_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M33_SLOT_B_SIZE           (CORTEX_M33_FLASH_SIZE / 3)
#define CORTEX_M33_RECOVERY_ADDR         (CORTEX_M33_FLASH_BASE + 2 * CORTEX_M33_FLASH_SIZE / 3)
#define CORTEX_M33_RECOVERY_SIZE         (CORTEX_M33_FLASH_SIZE / 8)
#define CORTEX_M33_BOOTCTL_ADDR          (CORTEX_M33_FLASH_BASE + CORTEX_M33_FLASH_SIZE - 0x2000)
#define CORTEX_M33_BOOTCTL_BACKUP_ADDR   (CORTEX_M33_FLASH_BASE + CORTEX_M33_FLASH_SIZE - 0x4000)
#define CORTEX_M33_LOG_ADDR              (CORTEX_M33_FLASH_BASE + CORTEX_M33_FLASH_SIZE - 0x6000)

#define CORTEX_M33_CPU_HZ                110000000
#define CORTEX_M33_BOARD_ID              0x3301

void board_cortex_m33_early_init(void);
const eos_board_ops_t *board_cortex_m33_get_ops(void);

#endif /* BOARD_CORTEX_M33_H */
