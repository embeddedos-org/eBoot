// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_alpha.h
 * @brief Alpha 21264 board configuration
 */

#ifndef BOARD_ALPHA_H
#define BOARD_ALPHA_H

#include "eos_hal.h"

#define ALPHA_FLASH_BASE            0x00000000
#define ALPHA_FLASH_SIZE            (4*1024*1024)
#define ALPHA_RAM_BASE              0x00000000
#define ALPHA_RAM_SIZE              (32768UL*1024*1024)

#define ALPHA_SLOT_A_ADDR           (ALPHA_FLASH_BASE + 0x20000)
#define ALPHA_SLOT_A_SIZE           (ALPHA_FLASH_SIZE / 3)
#define ALPHA_SLOT_B_ADDR           (ALPHA_FLASH_BASE + ALPHA_FLASH_SIZE / 3 + 0x20000)
#define ALPHA_SLOT_B_SIZE           (ALPHA_FLASH_SIZE / 3)
#define ALPHA_RECOVERY_ADDR         (ALPHA_FLASH_BASE + 2 * ALPHA_FLASH_SIZE / 3)
#define ALPHA_RECOVERY_SIZE         (ALPHA_FLASH_SIZE / 8)
#define ALPHA_BOOTCTL_ADDR          (ALPHA_FLASH_BASE + ALPHA_FLASH_SIZE - 0x2000)
#define ALPHA_BOOTCTL_BACKUP_ADDR   (ALPHA_FLASH_BASE + ALPHA_FLASH_SIZE - 0x4000)
#define ALPHA_LOG_ADDR              (ALPHA_FLASH_BASE + ALPHA_FLASH_SIZE - 0x6000)

#define ALPHA_CPU_HZ                1250000000
#define ALPHA_BOARD_ID              0x0A01

void board_alpha_early_init(void);
const eos_board_ops_t *board_alpha_get_ops(void);

#endif /* BOARD_ALPHA_H */
