// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_a9.h
 * @brief Zynq-7020 Cortex-A9 board configuration
 */

#ifndef BOARD_CORTEX_A9_H
#define BOARD_CORTEX_A9_H

#include "eos_hal.h"

#define CORTEX_A9_FLASH_BASE            0x00000000
#define CORTEX_A9_FLASH_SIZE            (4*1024*1024)
#define CORTEX_A9_RAM_BASE              0x00100000
#define CORTEX_A9_RAM_SIZE              (512*1024*1024)

#define CORTEX_A9_SLOT_A_ADDR           (CORTEX_A9_FLASH_BASE + 0x20000)
#define CORTEX_A9_SLOT_A_SIZE           (CORTEX_A9_FLASH_SIZE / 3)
#define CORTEX_A9_SLOT_B_ADDR           (CORTEX_A9_FLASH_BASE + CORTEX_A9_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_A9_SLOT_B_SIZE           (CORTEX_A9_FLASH_SIZE / 3)
#define CORTEX_A9_RECOVERY_ADDR         (CORTEX_A9_FLASH_BASE + 2 * CORTEX_A9_FLASH_SIZE / 3)
#define CORTEX_A9_RECOVERY_SIZE         (CORTEX_A9_FLASH_SIZE / 8)
#define CORTEX_A9_BOOTCTL_ADDR          (CORTEX_A9_FLASH_BASE + CORTEX_A9_FLASH_SIZE - 0x2000)
#define CORTEX_A9_BOOTCTL_BACKUP_ADDR   (CORTEX_A9_FLASH_BASE + CORTEX_A9_FLASH_SIZE - 0x4000)
#define CORTEX_A9_LOG_ADDR              (CORTEX_A9_FLASH_BASE + CORTEX_A9_FLASH_SIZE - 0x6000)

#define CORTEX_A9_CPU_HZ                866000000
#define CORTEX_A9_BOARD_ID              0xB901

void board_cortex_a9_early_init(void);
const eos_board_ops_t *board_cortex_a9_get_ops(void);

#endif /* BOARD_CORTEX_A9_H */
