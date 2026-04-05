// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_sparc64.h
 * @brief UltraSPARC T2 board configuration
 */

#ifndef BOARD_SPARC64_H
#define BOARD_SPARC64_H

#include "eos_hal.h"

#define SPARC64_FLASH_BASE            0x00000000
#define SPARC64_FLASH_SIZE            (4*1024*1024)
#define SPARC64_RAM_BASE              0x00000000
#define SPARC64_RAM_SIZE              (32768UL*1024*1024)

#define SPARC64_SLOT_A_ADDR           (SPARC64_FLASH_BASE + 0x20000)
#define SPARC64_SLOT_A_SIZE           (SPARC64_FLASH_SIZE / 3)
#define SPARC64_SLOT_B_ADDR           (SPARC64_FLASH_BASE + SPARC64_FLASH_SIZE / 3 + 0x20000)
#define SPARC64_SLOT_B_SIZE           (SPARC64_FLASH_SIZE / 3)
#define SPARC64_RECOVERY_ADDR         (SPARC64_FLASH_BASE + 2 * SPARC64_FLASH_SIZE / 3)
#define SPARC64_RECOVERY_SIZE         (SPARC64_FLASH_SIZE / 8)
#define SPARC64_BOOTCTL_ADDR          (SPARC64_FLASH_BASE + SPARC64_FLASH_SIZE - 0x2000)
#define SPARC64_BOOTCTL_BACKUP_ADDR   (SPARC64_FLASH_BASE + SPARC64_FLASH_SIZE - 0x4000)
#define SPARC64_LOG_ADDR              (SPARC64_FLASH_BASE + SPARC64_FLASH_SIZE - 0x6000)

#define SPARC64_CPU_HZ                1400000000
#define SPARC64_BOARD_ID              0x0501

void board_sparc64_early_init(void);
const eos_board_ops_t *board_sparc64_get_ops(void);

#endif /* BOARD_SPARC64_H */
