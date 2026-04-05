// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m3.h
 * @brief STM32F103 Cortex-M3 board configuration
 */

#ifndef BOARD_CORTEX_M3_H
#define BOARD_CORTEX_M3_H

#include "eos_hal.h"

#define CORTEX_M3_FLASH_BASE            0x08000000
#define CORTEX_M3_FLASH_SIZE            (512*1024)
#define CORTEX_M3_RAM_BASE              0x20000000
#define CORTEX_M3_RAM_SIZE              (64*1024)

#define CORTEX_M3_SLOT_A_ADDR           (CORTEX_M3_FLASH_BASE + 0x20000)
#define CORTEX_M3_SLOT_A_SIZE           (CORTEX_M3_FLASH_SIZE / 3)
#define CORTEX_M3_SLOT_B_ADDR           (CORTEX_M3_FLASH_BASE + CORTEX_M3_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M3_SLOT_B_SIZE           (CORTEX_M3_FLASH_SIZE / 3)
#define CORTEX_M3_RECOVERY_ADDR         (CORTEX_M3_FLASH_BASE + 2 * CORTEX_M3_FLASH_SIZE / 3)
#define CORTEX_M3_RECOVERY_SIZE         (CORTEX_M3_FLASH_SIZE / 8)
#define CORTEX_M3_BOOTCTL_ADDR          (CORTEX_M3_FLASH_BASE + CORTEX_M3_FLASH_SIZE - 0x2000)
#define CORTEX_M3_BOOTCTL_BACKUP_ADDR   (CORTEX_M3_FLASH_BASE + CORTEX_M3_FLASH_SIZE - 0x4000)
#define CORTEX_M3_LOG_ADDR              (CORTEX_M3_FLASH_BASE + CORTEX_M3_FLASH_SIZE - 0x6000)

#define CORTEX_M3_CPU_HZ                72000000
#define CORTEX_M3_BOARD_ID              0x0301

void board_cortex_m3_early_init(void);
const eos_board_ops_t *board_cortex_m3_get_ops(void);

#endif /* BOARD_CORTEX_M3_H */
