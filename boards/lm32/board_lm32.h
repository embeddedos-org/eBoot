// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_lm32.h
 * @brief LatticeMico32 board configuration
 */

#ifndef BOARD_LM32_H
#define BOARD_LM32_H

#include "eos_hal.h"

#define LM32_FLASH_BASE            0x00000000
#define LM32_FLASH_SIZE            (4*1024*1024)
#define LM32_RAM_BASE              0x00000000
#define LM32_RAM_SIZE              (8*1024*1024)

#define LM32_SLOT_A_ADDR           (LM32_FLASH_BASE + 0x20000)
#define LM32_SLOT_A_SIZE           (LM32_FLASH_SIZE / 3)
#define LM32_SLOT_B_ADDR           (LM32_FLASH_BASE + LM32_FLASH_SIZE / 3 + 0x20000)
#define LM32_SLOT_B_SIZE           (LM32_FLASH_SIZE / 3)
#define LM32_RECOVERY_ADDR         (LM32_FLASH_BASE + 2 * LM32_FLASH_SIZE / 3)
#define LM32_RECOVERY_SIZE         (LM32_FLASH_SIZE / 8)
#define LM32_BOOTCTL_ADDR          (LM32_FLASH_BASE + LM32_FLASH_SIZE - 0x2000)
#define LM32_BOOTCTL_BACKUP_ADDR   (LM32_FLASH_BASE + LM32_FLASH_SIZE - 0x4000)
#define LM32_LOG_ADDR              (LM32_FLASH_BASE + LM32_FLASH_SIZE - 0x6000)

#define LM32_CPU_HZ                100000000
#define LM32_BOARD_ID              0xF801

void board_lm32_early_init(void);
const eos_board_ops_t *board_lm32_get_ops(void);

#endif /* BOARD_LM32_H */
