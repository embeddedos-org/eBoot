// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_a15.h
 * @brief OMAP5432 Cortex-A15 board configuration
 */

#ifndef BOARD_CORTEX_A15_H
#define BOARD_CORTEX_A15_H

#include "eos_hal.h"

#define CORTEX_A15_FLASH_BASE            0x00000000
#define CORTEX_A15_FLASH_SIZE            (4*1024*1024)
#define CORTEX_A15_RAM_BASE              0x80000000
#define CORTEX_A15_RAM_SIZE              (2048UL*1024*1024)

#define CORTEX_A15_SLOT_A_ADDR           (CORTEX_A15_FLASH_BASE + 0x20000)
#define CORTEX_A15_SLOT_A_SIZE           (CORTEX_A15_FLASH_SIZE / 3)
#define CORTEX_A15_SLOT_B_ADDR           (CORTEX_A15_FLASH_BASE + CORTEX_A15_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_A15_SLOT_B_SIZE           (CORTEX_A15_FLASH_SIZE / 3)
#define CORTEX_A15_RECOVERY_ADDR         (CORTEX_A15_FLASH_BASE + 2 * CORTEX_A15_FLASH_SIZE / 3)
#define CORTEX_A15_RECOVERY_SIZE         (CORTEX_A15_FLASH_SIZE / 8)
#define CORTEX_A15_BOOTCTL_ADDR          (CORTEX_A15_FLASH_BASE + CORTEX_A15_FLASH_SIZE - 0x2000)
#define CORTEX_A15_BOOTCTL_BACKUP_ADDR   (CORTEX_A15_FLASH_BASE + CORTEX_A15_FLASH_SIZE - 0x4000)
#define CORTEX_A15_LOG_ADDR              (CORTEX_A15_FLASH_BASE + CORTEX_A15_FLASH_SIZE - 0x6000)

#define CORTEX_A15_CPU_HZ                1500000000
#define CORTEX_A15_BOARD_ID              0xBA51

void board_cortex_a15_early_init(void);
const eos_board_ops_t *board_cortex_a15_get_ops(void);

#endif /* BOARD_CORTEX_A15_H */
