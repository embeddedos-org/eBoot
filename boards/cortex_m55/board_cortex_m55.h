// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m55.h
 * @brief Corstone-300 Cortex-M55 board configuration
 */

#ifndef BOARD_CORTEX_M55_H
#define BOARD_CORTEX_M55_H

#include "eos_hal.h"

#define CORTEX_M55_FLASH_BASE            0x10000000
#define CORTEX_M55_FLASH_SIZE            (2*1024*1024)
#define CORTEX_M55_RAM_BASE              0x20000000
#define CORTEX_M55_RAM_SIZE              (512*1024)

#define CORTEX_M55_SLOT_A_ADDR           (CORTEX_M55_FLASH_BASE + 0x20000)
#define CORTEX_M55_SLOT_A_SIZE           (CORTEX_M55_FLASH_SIZE / 3)
#define CORTEX_M55_SLOT_B_ADDR           (CORTEX_M55_FLASH_BASE + CORTEX_M55_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M55_SLOT_B_SIZE           (CORTEX_M55_FLASH_SIZE / 3)
#define CORTEX_M55_RECOVERY_ADDR         (CORTEX_M55_FLASH_BASE + 2 * CORTEX_M55_FLASH_SIZE / 3)
#define CORTEX_M55_RECOVERY_SIZE         (CORTEX_M55_FLASH_SIZE / 8)
#define CORTEX_M55_BOOTCTL_ADDR          (CORTEX_M55_FLASH_BASE + CORTEX_M55_FLASH_SIZE - 0x2000)
#define CORTEX_M55_BOOTCTL_BACKUP_ADDR   (CORTEX_M55_FLASH_BASE + CORTEX_M55_FLASH_SIZE - 0x4000)
#define CORTEX_M55_LOG_ADDR              (CORTEX_M55_FLASH_BASE + CORTEX_M55_FLASH_SIZE - 0x6000)

#define CORTEX_M55_CPU_HZ                250000000
#define CORTEX_M55_BOARD_ID              0x5501

void board_cortex_m55_early_init(void);
const eos_board_ops_t *board_cortex_m55_get_ops(void);

#endif /* BOARD_CORTEX_M55_H */
