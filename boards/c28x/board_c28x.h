// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_c28x.h
 * @brief TMS320F28379D C28x board configuration
 */

#ifndef BOARD_C28X_H
#define BOARD_C28X_H

#include "eos_hal.h"

#define C28X_FLASH_BASE            0x00080000
#define C28X_FLASH_SIZE            (1024*1024)
#define C28X_RAM_BASE              0x00000000
#define C28X_RAM_SIZE              (200*1024)

#define C28X_SLOT_A_ADDR           (C28X_FLASH_BASE + 0x20000)
#define C28X_SLOT_A_SIZE           (C28X_FLASH_SIZE / 3)
#define C28X_SLOT_B_ADDR           (C28X_FLASH_BASE + C28X_FLASH_SIZE / 3 + 0x20000)
#define C28X_SLOT_B_SIZE           (C28X_FLASH_SIZE / 3)
#define C28X_RECOVERY_ADDR         (C28X_FLASH_BASE + 2 * C28X_FLASH_SIZE / 3)
#define C28X_RECOVERY_SIZE         (C28X_FLASH_SIZE / 8)
#define C28X_BOOTCTL_ADDR          (C28X_FLASH_BASE + C28X_FLASH_SIZE - 0x2000)
#define C28X_BOOTCTL_BACKUP_ADDR   (C28X_FLASH_BASE + C28X_FLASH_SIZE - 0x4000)
#define C28X_LOG_ADDR              (C28X_FLASH_BASE + C28X_FLASH_SIZE - 0x6000)

#define C28X_CPU_HZ                200000000
#define C28X_BOARD_ID              0xE201

void board_c28x_early_init(void);
const eos_board_ops_t *board_c28x_get_ops(void);

#endif /* BOARD_C28X_H */
