// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_kalimba.h
 * @brief CSR8675 Kalimba board configuration
 */

#ifndef BOARD_KALIMBA_H
#define BOARD_KALIMBA_H

#include "eos_hal.h"

#define KALIMBA_FLASH_BASE            0x00000000
#define KALIMBA_FLASH_SIZE            (4*1024*1024)
#define KALIMBA_RAM_BASE              0x00000000
#define KALIMBA_RAM_SIZE              (1024*1024)

#define KALIMBA_SLOT_A_ADDR           (KALIMBA_FLASH_BASE + 0x20000)
#define KALIMBA_SLOT_A_SIZE           (KALIMBA_FLASH_SIZE / 3)
#define KALIMBA_SLOT_B_ADDR           (KALIMBA_FLASH_BASE + KALIMBA_FLASH_SIZE / 3 + 0x20000)
#define KALIMBA_SLOT_B_SIZE           (KALIMBA_FLASH_SIZE / 3)
#define KALIMBA_RECOVERY_ADDR         (KALIMBA_FLASH_BASE + 2 * KALIMBA_FLASH_SIZE / 3)
#define KALIMBA_RECOVERY_SIZE         (KALIMBA_FLASH_SIZE / 8)
#define KALIMBA_BOOTCTL_ADDR          (KALIMBA_FLASH_BASE + KALIMBA_FLASH_SIZE - 0x2000)
#define KALIMBA_BOOTCTL_BACKUP_ADDR   (KALIMBA_FLASH_BASE + KALIMBA_FLASH_SIZE - 0x4000)
#define KALIMBA_LOG_ADDR              (KALIMBA_FLASH_BASE + KALIMBA_FLASH_SIZE - 0x6000)

#define KALIMBA_CPU_HZ                120000000
#define KALIMBA_BOARD_ID              0x0D01

void board_kalimba_early_init(void);
const eos_board_ops_t *board_kalimba_get_ops(void);

#endif /* BOARD_KALIMBA_H */
