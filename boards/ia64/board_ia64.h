// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_ia64.h
 * @brief Itanium 9500 board configuration
 */

#ifndef BOARD_IA64_H
#define BOARD_IA64_H

#include "eos_hal.h"

#define IA64_FLASH_BASE            0x00000000
#define IA64_FLASH_SIZE            (4*1024*1024)
#define IA64_RAM_BASE              0x00000000
#define IA64_RAM_SIZE              (524288UL*1024*1024)

#define IA64_SLOT_A_ADDR           (IA64_FLASH_BASE + 0x20000)
#define IA64_SLOT_A_SIZE           (IA64_FLASH_SIZE / 3)
#define IA64_SLOT_B_ADDR           (IA64_FLASH_BASE + IA64_FLASH_SIZE / 3 + 0x20000)
#define IA64_SLOT_B_SIZE           (IA64_FLASH_SIZE / 3)
#define IA64_RECOVERY_ADDR         (IA64_FLASH_BASE + 2 * IA64_FLASH_SIZE / 3)
#define IA64_RECOVERY_SIZE         (IA64_FLASH_SIZE / 8)
#define IA64_BOOTCTL_ADDR          (IA64_FLASH_BASE + IA64_FLASH_SIZE - 0x2000)
#define IA64_BOOTCTL_BACKUP_ADDR   (IA64_FLASH_BASE + IA64_FLASH_SIZE - 0x4000)
#define IA64_LOG_ADDR              (IA64_FLASH_BASE + IA64_FLASH_SIZE - 0x6000)

#define IA64_CPU_HZ                2530000000
#define IA64_BOARD_ID              0x0901

void board_ia64_early_init(void);
const eos_board_ops_t *board_ia64_get_ops(void);

#endif /* BOARD_IA64_H */
