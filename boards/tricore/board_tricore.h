// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_tricore.h
 * @brief AURIX TC397 TriCore board configuration
 */

#ifndef BOARD_TRICORE_H
#define BOARD_TRICORE_H

#include "eos_hal.h"

#define TRICORE_FLASH_BASE            0x80000000
#define TRICORE_FLASH_SIZE            (16*1024*1024)
#define TRICORE_RAM_BASE              0x70000000
#define TRICORE_RAM_SIZE              (6*1024*1024)

#define TRICORE_SLOT_A_ADDR           (TRICORE_FLASH_BASE + 0x20000)
#define TRICORE_SLOT_A_SIZE           (TRICORE_FLASH_SIZE / 3)
#define TRICORE_SLOT_B_ADDR           (TRICORE_FLASH_BASE + TRICORE_FLASH_SIZE / 3 + 0x20000)
#define TRICORE_SLOT_B_SIZE           (TRICORE_FLASH_SIZE / 3)
#define TRICORE_RECOVERY_ADDR         (TRICORE_FLASH_BASE + 2 * TRICORE_FLASH_SIZE / 3)
#define TRICORE_RECOVERY_SIZE         (TRICORE_FLASH_SIZE / 8)
#define TRICORE_BOOTCTL_ADDR          (TRICORE_FLASH_BASE + TRICORE_FLASH_SIZE - 0x2000)
#define TRICORE_BOOTCTL_BACKUP_ADDR   (TRICORE_FLASH_BASE + TRICORE_FLASH_SIZE - 0x4000)
#define TRICORE_LOG_ADDR              (TRICORE_FLASH_BASE + TRICORE_FLASH_SIZE - 0x6000)

#define TRICORE_CPU_HZ                300000000
#define TRICORE_BOARD_ID              0xF301

void board_tricore_early_init(void);
const eos_board_ops_t *board_tricore_get_ops(void);

#endif /* BOARD_TRICORE_H */
