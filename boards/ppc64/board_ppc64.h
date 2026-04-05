// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_ppc64.h
 * @brief POWER9 board configuration
 */

#ifndef BOARD_PPC64_H
#define BOARD_PPC64_H

#include "eos_hal.h"

#define PPC64_FLASH_BASE            0x00000000
#define PPC64_FLASH_SIZE            (4*1024*1024)
#define PPC64_RAM_BASE              0x00000000
#define PPC64_RAM_SIZE              (65536UL*1024*1024)

#define PPC64_SLOT_A_ADDR           (PPC64_FLASH_BASE + 0x20000)
#define PPC64_SLOT_A_SIZE           (PPC64_FLASH_SIZE / 3)
#define PPC64_SLOT_B_ADDR           (PPC64_FLASH_BASE + PPC64_FLASH_SIZE / 3 + 0x20000)
#define PPC64_SLOT_B_SIZE           (PPC64_FLASH_SIZE / 3)
#define PPC64_RECOVERY_ADDR         (PPC64_FLASH_BASE + 2 * PPC64_FLASH_SIZE / 3)
#define PPC64_RECOVERY_SIZE         (PPC64_FLASH_SIZE / 8)
#define PPC64_BOOTCTL_ADDR          (PPC64_FLASH_BASE + PPC64_FLASH_SIZE - 0x2000)
#define PPC64_BOOTCTL_BACKUP_ADDR   (PPC64_FLASH_BASE + PPC64_FLASH_SIZE - 0x4000)
#define PPC64_LOG_ADDR              (PPC64_FLASH_BASE + PPC64_FLASH_SIZE - 0x6000)

#define PPC64_CPU_HZ                4000000000
#define PPC64_BOARD_ID              0x0601

void board_ppc64_early_init(void);
const eos_board_ops_t *board_ppc64_get_ops(void);

#endif /* BOARD_PPC64_H */
