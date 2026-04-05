// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_cris.h
 * @brief ETRAX 100LX board configuration
 */

#ifndef BOARD_CRIS_H
#define BOARD_CRIS_H

#include "eos_hal.h"

#define CRIS_FLASH_BASE            0x00000000
#define CRIS_FLASH_SIZE            (4*1024*1024)
#define CRIS_RAM_BASE              0x40000000
#define CRIS_RAM_SIZE              (64*1024*1024)

#define CRIS_SLOT_A_ADDR           (CRIS_FLASH_BASE + 0x20000)
#define CRIS_SLOT_A_SIZE           (CRIS_FLASH_SIZE / 3)
#define CRIS_SLOT_B_ADDR           (CRIS_FLASH_BASE + CRIS_FLASH_SIZE / 3 + 0x20000)
#define CRIS_SLOT_B_SIZE           (CRIS_FLASH_SIZE / 3)
#define CRIS_RECOVERY_ADDR         (CRIS_FLASH_BASE + 2 * CRIS_FLASH_SIZE / 3)
#define CRIS_RECOVERY_SIZE         (CRIS_FLASH_SIZE / 8)
#define CRIS_BOOTCTL_ADDR          (CRIS_FLASH_BASE + CRIS_FLASH_SIZE - 0x2000)
#define CRIS_BOOTCTL_BACKUP_ADDR   (CRIS_FLASH_BASE + CRIS_FLASH_SIZE - 0x4000)
#define CRIS_LOG_ADDR              (CRIS_FLASH_BASE + CRIS_FLASH_SIZE - 0x6000)

#define CRIS_CPU_HZ                200000000
#define CRIS_BOARD_ID              0x0C01

void board_cris_early_init(void);
const eos_board_ops_t *board_cris_get_ops(void);

#endif /* BOARD_CRIS_H */
