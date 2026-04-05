// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_a5.h
 * @brief SAMA5D36 Cortex-A5 board configuration
 */

#ifndef BOARD_CORTEX_A5_H
#define BOARD_CORTEX_A5_H

#include "eos_hal.h"

#define CORTEX_A5_FLASH_BASE            0x00000000
#define CORTEX_A5_FLASH_SIZE            (4*1024*1024)
#define CORTEX_A5_RAM_BASE              0x20000000
#define CORTEX_A5_RAM_SIZE              (256*1024*1024)

#define CORTEX_A5_SLOT_A_ADDR           (CORTEX_A5_FLASH_BASE + 0x20000)
#define CORTEX_A5_SLOT_A_SIZE           (CORTEX_A5_FLASH_SIZE / 3)
#define CORTEX_A5_SLOT_B_ADDR           (CORTEX_A5_FLASH_BASE + CORTEX_A5_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_A5_SLOT_B_SIZE           (CORTEX_A5_FLASH_SIZE / 3)
#define CORTEX_A5_RECOVERY_ADDR         (CORTEX_A5_FLASH_BASE + 2 * CORTEX_A5_FLASH_SIZE / 3)
#define CORTEX_A5_RECOVERY_SIZE         (CORTEX_A5_FLASH_SIZE / 8)
#define CORTEX_A5_BOOTCTL_ADDR          (CORTEX_A5_FLASH_BASE + CORTEX_A5_FLASH_SIZE - 0x2000)
#define CORTEX_A5_BOOTCTL_BACKUP_ADDR   (CORTEX_A5_FLASH_BASE + CORTEX_A5_FLASH_SIZE - 0x4000)
#define CORTEX_A5_LOG_ADDR              (CORTEX_A5_FLASH_BASE + CORTEX_A5_FLASH_SIZE - 0x6000)

#define CORTEX_A5_CPU_HZ                536000000
#define CORTEX_A5_BOARD_ID              0xB501

void board_cortex_a5_early_init(void);
const eos_board_ops_t *board_cortex_a5_get_ops(void);

#endif /* BOARD_CORTEX_A5_H */
