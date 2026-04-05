// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arc.h
 * @brief ARC EM9D board configuration
 */

#ifndef BOARD_ARC_H
#define BOARD_ARC_H

#include "eos_hal.h"

#define ARC_FLASH_BASE            0x00000000
#define ARC_FLASH_SIZE            (1024*1024)
#define ARC_RAM_BASE              0x00000000
#define ARC_RAM_SIZE              (256*1024)

#define ARC_SLOT_A_ADDR           (ARC_FLASH_BASE + 0x20000)
#define ARC_SLOT_A_SIZE           (ARC_FLASH_SIZE / 3)
#define ARC_SLOT_B_ADDR           (ARC_FLASH_BASE + ARC_FLASH_SIZE / 3 + 0x20000)
#define ARC_SLOT_B_SIZE           (ARC_FLASH_SIZE / 3)
#define ARC_RECOVERY_ADDR         (ARC_FLASH_BASE + 2 * ARC_FLASH_SIZE / 3)
#define ARC_RECOVERY_SIZE         (ARC_FLASH_SIZE / 8)
#define ARC_BOOTCTL_ADDR          (ARC_FLASH_BASE + ARC_FLASH_SIZE - 0x2000)
#define ARC_BOOTCTL_BACKUP_ADDR   (ARC_FLASH_BASE + ARC_FLASH_SIZE - 0x4000)
#define ARC_LOG_ADDR              (ARC_FLASH_BASE + ARC_FLASH_SIZE - 0x6000)

#define ARC_CPU_HZ                100000000
#define ARC_BOARD_ID              0xFE01

void board_arc_early_init(void);
const eos_board_ops_t *board_arc_get_ops(void);

#endif /* BOARD_ARC_H */
