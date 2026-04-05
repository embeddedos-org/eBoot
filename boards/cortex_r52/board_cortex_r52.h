// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_r52.h
 * @brief Cortex-R52 Reference board configuration
 */

#ifndef BOARD_CORTEX_R52_H
#define BOARD_CORTEX_R52_H

#include "eos_hal.h"

#define CORTEX_R52_FLASH_BASE            0x00000000
#define CORTEX_R52_FLASH_SIZE            (4*1024*1024)
#define CORTEX_R52_RAM_BASE              0x20000000
#define CORTEX_R52_RAM_SIZE              (1024*1024)

#define CORTEX_R52_SLOT_A_ADDR           (CORTEX_R52_FLASH_BASE + 0x20000)
#define CORTEX_R52_SLOT_A_SIZE           (CORTEX_R52_FLASH_SIZE / 3)
#define CORTEX_R52_SLOT_B_ADDR           (CORTEX_R52_FLASH_BASE + CORTEX_R52_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_R52_SLOT_B_SIZE           (CORTEX_R52_FLASH_SIZE / 3)
#define CORTEX_R52_RECOVERY_ADDR         (CORTEX_R52_FLASH_BASE + 2 * CORTEX_R52_FLASH_SIZE / 3)
#define CORTEX_R52_RECOVERY_SIZE         (CORTEX_R52_FLASH_SIZE / 8)
#define CORTEX_R52_BOOTCTL_ADDR          (CORTEX_R52_FLASH_BASE + CORTEX_R52_FLASH_SIZE - 0x2000)
#define CORTEX_R52_BOOTCTL_BACKUP_ADDR   (CORTEX_R52_FLASH_BASE + CORTEX_R52_FLASH_SIZE - 0x4000)
#define CORTEX_R52_LOG_ADDR              (CORTEX_R52_FLASH_BASE + CORTEX_R52_FLASH_SIZE - 0x6000)

#define CORTEX_R52_CPU_HZ                800000000
#define CORTEX_R52_BOARD_ID              0xA521

void board_cortex_r52_early_init(void);
const eos_board_ops_t *board_cortex_r52_get_ops(void);

#endif /* BOARD_CORTEX_R52_H */
