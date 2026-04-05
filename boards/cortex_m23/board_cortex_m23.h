// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m23.h
 * @brief LPC55S06 Cortex-M23 board configuration
 */

#ifndef BOARD_CORTEX_M23_H
#define BOARD_CORTEX_M23_H

#include "eos_hal.h"

#define CORTEX_M23_FLASH_BASE            0x00000000
#define CORTEX_M23_FLASH_SIZE            (256*1024)
#define CORTEX_M23_RAM_BASE              0x20000000
#define CORTEX_M23_RAM_SIZE              (96*1024)

#define CORTEX_M23_SLOT_A_ADDR           (CORTEX_M23_FLASH_BASE + 0x20000)
#define CORTEX_M23_SLOT_A_SIZE           (CORTEX_M23_FLASH_SIZE / 3)
#define CORTEX_M23_SLOT_B_ADDR           (CORTEX_M23_FLASH_BASE + CORTEX_M23_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M23_SLOT_B_SIZE           (CORTEX_M23_FLASH_SIZE / 3)
#define CORTEX_M23_RECOVERY_ADDR         (CORTEX_M23_FLASH_BASE + 2 * CORTEX_M23_FLASH_SIZE / 3)
#define CORTEX_M23_RECOVERY_SIZE         (CORTEX_M23_FLASH_SIZE / 8)
#define CORTEX_M23_BOOTCTL_ADDR          (CORTEX_M23_FLASH_BASE + CORTEX_M23_FLASH_SIZE - 0x2000)
#define CORTEX_M23_BOOTCTL_BACKUP_ADDR   (CORTEX_M23_FLASH_BASE + CORTEX_M23_FLASH_SIZE - 0x4000)
#define CORTEX_M23_LOG_ADDR              (CORTEX_M23_FLASH_BASE + CORTEX_M23_FLASH_SIZE - 0x6000)

#define CORTEX_M23_CPU_HZ                96000000
#define CORTEX_M23_BOARD_ID              0x2301

void board_cortex_m23_early_init(void);
const eos_board_ops_t *board_cortex_m23_get_ops(void);

#endif /* BOARD_CORTEX_M23_H */
