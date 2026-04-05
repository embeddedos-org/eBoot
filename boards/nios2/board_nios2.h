// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_nios2.h
 * @brief Nios II Intel/Altera board configuration
 */

#ifndef BOARD_NIOS2_H
#define BOARD_NIOS2_H

#include "eos_hal.h"

#define NIOS2_FLASH_BASE            0x00000000
#define NIOS2_FLASH_SIZE            (16*1024*1024)
#define NIOS2_RAM_BASE              0x00000000
#define NIOS2_RAM_SIZE              (128*1024*1024)

#define NIOS2_SLOT_A_ADDR           (NIOS2_FLASH_BASE + 0x20000)
#define NIOS2_SLOT_A_SIZE           (NIOS2_FLASH_SIZE / 3)
#define NIOS2_SLOT_B_ADDR           (NIOS2_FLASH_BASE + NIOS2_FLASH_SIZE / 3 + 0x20000)
#define NIOS2_SLOT_B_SIZE           (NIOS2_FLASH_SIZE / 3)
#define NIOS2_RECOVERY_ADDR         (NIOS2_FLASH_BASE + 2 * NIOS2_FLASH_SIZE / 3)
#define NIOS2_RECOVERY_SIZE         (NIOS2_FLASH_SIZE / 8)
#define NIOS2_BOOTCTL_ADDR          (NIOS2_FLASH_BASE + NIOS2_FLASH_SIZE - 0x2000)
#define NIOS2_BOOTCTL_BACKUP_ADDR   (NIOS2_FLASH_BASE + NIOS2_FLASH_SIZE - 0x4000)
#define NIOS2_LOG_ADDR              (NIOS2_FLASH_BASE + NIOS2_FLASH_SIZE - 0x6000)

#define NIOS2_CPU_HZ                250000000
#define NIOS2_BOARD_ID              0xF601

void board_nios2_early_init(void);
const eos_board_ops_t *board_nios2_get_ops(void);

#endif /* BOARD_NIOS2_H */
