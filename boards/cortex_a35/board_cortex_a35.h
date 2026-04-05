// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_a35.h
 * @brief i.MX8X Cortex-A35 board configuration
 */

#ifndef BOARD_CORTEX_A35_H
#define BOARD_CORTEX_A35_H

#include "eos_hal.h"

#define CORTEX_A35_FLASH_BASE            0x00000000
#define CORTEX_A35_FLASH_SIZE            (4*1024*1024)
#define CORTEX_A35_RAM_BASE              0x80000000
#define CORTEX_A35_RAM_SIZE              (2048UL*1024*1024)

#define CORTEX_A35_SLOT_A_ADDR           (CORTEX_A35_FLASH_BASE + 0x20000)
#define CORTEX_A35_SLOT_A_SIZE           (CORTEX_A35_FLASH_SIZE / 3)
#define CORTEX_A35_SLOT_B_ADDR           (CORTEX_A35_FLASH_BASE + CORTEX_A35_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_A35_SLOT_B_SIZE           (CORTEX_A35_FLASH_SIZE / 3)
#define CORTEX_A35_RECOVERY_ADDR         (CORTEX_A35_FLASH_BASE + 2 * CORTEX_A35_FLASH_SIZE / 3)
#define CORTEX_A35_RECOVERY_SIZE         (CORTEX_A35_FLASH_SIZE / 8)
#define CORTEX_A35_BOOTCTL_ADDR          (CORTEX_A35_FLASH_BASE + CORTEX_A35_FLASH_SIZE - 0x2000)
#define CORTEX_A35_BOOTCTL_BACKUP_ADDR   (CORTEX_A35_FLASH_BASE + CORTEX_A35_FLASH_SIZE - 0x4000)
#define CORTEX_A35_LOG_ADDR              (CORTEX_A35_FLASH_BASE + CORTEX_A35_FLASH_SIZE - 0x6000)

#define CORTEX_A35_CPU_HZ                1200000000
#define CORTEX_A35_BOARD_ID              0xBB51

void board_cortex_a35_early_init(void);
const eos_board_ops_t *board_cortex_a35_get_ops(void);

#endif /* BOARD_CORTEX_A35_H */
