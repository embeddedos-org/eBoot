// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_mips64.h
 * @brief MIPS64 R6 board configuration
 */

#ifndef BOARD_MIPS64_H
#define BOARD_MIPS64_H

#include "eos_hal.h"

#define MIPS64_FLASH_BASE            0x00000000
#define MIPS64_FLASH_SIZE            (4*1024*1024)
#define MIPS64_RAM_BASE              0x00000000
#define MIPS64_RAM_SIZE              (4096UL*1024*1024)

#define MIPS64_SLOT_A_ADDR           (MIPS64_FLASH_BASE + 0x20000)
#define MIPS64_SLOT_A_SIZE           (MIPS64_FLASH_SIZE / 3)
#define MIPS64_SLOT_B_ADDR           (MIPS64_FLASH_BASE + MIPS64_FLASH_SIZE / 3 + 0x20000)
#define MIPS64_SLOT_B_SIZE           (MIPS64_FLASH_SIZE / 3)
#define MIPS64_RECOVERY_ADDR         (MIPS64_FLASH_BASE + 2 * MIPS64_FLASH_SIZE / 3)
#define MIPS64_RECOVERY_SIZE         (MIPS64_FLASH_SIZE / 8)
#define MIPS64_BOOTCTL_ADDR          (MIPS64_FLASH_BASE + MIPS64_FLASH_SIZE - 0x2000)
#define MIPS64_BOOTCTL_BACKUP_ADDR   (MIPS64_FLASH_BASE + MIPS64_FLASH_SIZE - 0x4000)
#define MIPS64_LOG_ADDR              (MIPS64_FLASH_BASE + MIPS64_FLASH_SIZE - 0x6000)

#define MIPS64_CPU_HZ                1500000000
#define MIPS64_BOARD_ID              0x0401

void board_mips64_early_init(void);
const eos_board_ops_t *board_mips64_get_ops(void);

#endif /* BOARD_MIPS64_H */
