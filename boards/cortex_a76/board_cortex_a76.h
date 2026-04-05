// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cortex_a76.h
 * @brief RK3588 Cortex-A76 board configuration
 */

#ifndef BOARD_CORTEX_A76_H
#define BOARD_CORTEX_A76_H

#include "eos_hal.h"

#define CORTEX_A76_FLASH_BASE            0x00000000
#define CORTEX_A76_FLASH_SIZE            (4*1024*1024)
#define CORTEX_A76_RAM_BASE              0x00200000
#define CORTEX_A76_RAM_SIZE              (8192UL*1024*1024)

#define CORTEX_A76_SLOT_A_ADDR           (CORTEX_A76_FLASH_BASE + 0x20000)
#define CORTEX_A76_SLOT_A_SIZE           (CORTEX_A76_FLASH_SIZE / 3)
#define CORTEX_A76_SLOT_B_ADDR           (CORTEX_A76_FLASH_BASE + CORTEX_A76_FLASH_SIZE / 3 + 0x20000)
#define CORTEX_A76_SLOT_B_SIZE           (CORTEX_A76_FLASH_SIZE / 3)
#define CORTEX_A76_RECOVERY_ADDR         (CORTEX_A76_FLASH_BASE + 2 * CORTEX_A76_FLASH_SIZE / 3)
#define CORTEX_A76_RECOVERY_SIZE         (CORTEX_A76_FLASH_SIZE / 8)
#define CORTEX_A76_BOOTCTL_ADDR          (CORTEX_A76_FLASH_BASE + CORTEX_A76_FLASH_SIZE - 0x2000)
#define CORTEX_A76_BOOTCTL_BACKUP_ADDR   (CORTEX_A76_FLASH_BASE + CORTEX_A76_FLASH_SIZE - 0x4000)
#define CORTEX_A76_LOG_ADDR              (CORTEX_A76_FLASH_BASE + CORTEX_A76_FLASH_SIZE - 0x6000)

#define CORTEX_A76_CPU_HZ                2400000000
#define CORTEX_A76_BOARD_ID              0xBD76

void board_cortex_a76_early_init(void);
const eos_board_ops_t *board_cortex_a76_get_ops(void);

#endif /* BOARD_CORTEX_A76_H */
