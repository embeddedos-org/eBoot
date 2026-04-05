// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_r4.h
 * @brief RM46L852 Cortex-R4F board configuration
 */

#ifndef BOARD_CORTEX_R4_H
#define BOARD_CORTEX_R4_H

#include "eos_hal.h"

#define CORTEX_R4_FLASH_BASE            0x00000000
#define CORTEX_R4_FLASH_SIZE            (1280*1024)
#define CORTEX_R4_RAM_BASE              0x08000000
#define CORTEX_R4_RAM_SIZE              (256*1024)

#define CORTEX_R4_SLOT_A_ADDR           (CORTEX_R4_FLASH_BASE + 0x20000)
#define CORTEX_R4_SLOT_A_SIZE           (CORTEX_R4_FLASH_SIZE / 3)
#define CORTEX_R4_SLOT_B_ADDR           (CORTEX_R4_FLASH_BASE + CORTEX_R4_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_R4_SLOT_B_SIZE           (CORTEX_R4_FLASH_SIZE / 3)
#define CORTEX_R4_RECOVERY_ADDR         (CORTEX_R4_FLASH_BASE + 2 * CORTEX_R4_FLASH_SIZE / 3)
#define CORTEX_R4_RECOVERY_SIZE         (CORTEX_R4_FLASH_SIZE / 8)
#define CORTEX_R4_BOOTCTL_ADDR          (CORTEX_R4_FLASH_BASE + CORTEX_R4_FLASH_SIZE - 0x2000)
#define CORTEX_R4_BOOTCTL_BACKUP_ADDR   (CORTEX_R4_FLASH_BASE + CORTEX_R4_FLASH_SIZE - 0x4000)
#define CORTEX_R4_LOG_ADDR              (CORTEX_R4_FLASH_BASE + CORTEX_R4_FLASH_SIZE - 0x6000)

#define CORTEX_R4_CPU_HZ                220000000
#define CORTEX_R4_BOARD_ID              0xA401

void board_cortex_r4_early_init(void);
const eos_board_ops_t *board_cortex_r4_get_ops(void);

#endif /* BOARD_CORTEX_R4_H */
