// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_m85.h
 * @brief RA8M1 Cortex-M85 board configuration
 */

#ifndef BOARD_CORTEX_M85_H
#define BOARD_CORTEX_M85_H

#include "eos_hal.h"

#define CORTEX_M85_FLASH_BASE            0x00000000
#define CORTEX_M85_FLASH_SIZE            (2*1024*1024)
#define CORTEX_M85_RAM_BASE              0x20000000
#define CORTEX_M85_RAM_SIZE              (1024*1024)

#define CORTEX_M85_SLOT_A_ADDR           (CORTEX_M85_FLASH_BASE + 0x20000)
#define CORTEX_M85_SLOT_A_SIZE           (CORTEX_M85_FLASH_SIZE / 3)
#define CORTEX_M85_SLOT_B_ADDR           (CORTEX_M85_FLASH_BASE + CORTEX_M85_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_M85_SLOT_B_SIZE           (CORTEX_M85_FLASH_SIZE / 3)
#define CORTEX_M85_RECOVERY_ADDR         (CORTEX_M85_FLASH_BASE + 2 * CORTEX_M85_FLASH_SIZE / 3)
#define CORTEX_M85_RECOVERY_SIZE         (CORTEX_M85_FLASH_SIZE / 8)
#define CORTEX_M85_BOOTCTL_ADDR          (CORTEX_M85_FLASH_BASE + CORTEX_M85_FLASH_SIZE - 0x2000)
#define CORTEX_M85_BOOTCTL_BACKUP_ADDR   (CORTEX_M85_FLASH_BASE + CORTEX_M85_FLASH_SIZE - 0x4000)
#define CORTEX_M85_LOG_ADDR              (CORTEX_M85_FLASH_BASE + CORTEX_M85_FLASH_SIZE - 0x6000)

#define CORTEX_M85_CPU_HZ                480000000
#define CORTEX_M85_BOARD_ID              0x8501

void board_cortex_m85_early_init(void);
const eos_board_ops_t *board_cortex_m85_get_ops(void);

#endif /* BOARD_CORTEX_M85_H */
