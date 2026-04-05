// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_rx.h
 * @brief RX65N board configuration
 */

#ifndef BOARD_RX_H
#define BOARD_RX_H

#include "eos_hal.h"

#define RX_FLASH_BASE            0xFFE00000
#define RX_FLASH_SIZE            (2*1024*1024)
#define RX_RAM_BASE              0x00000000
#define RX_RAM_SIZE              (640*1024)

#define RX_SLOT_A_ADDR           (RX_FLASH_BASE + 0x20000)
#define RX_SLOT_A_SIZE           (RX_FLASH_SIZE / 3)
#define RX_SLOT_B_ADDR           (RX_FLASH_BASE + RX_FLASH_SIZE / 3 + 0x20000)
#define RX_SLOT_B_SIZE           (RX_FLASH_SIZE / 3)
#define RX_RECOVERY_ADDR         (RX_FLASH_BASE + 2 * RX_FLASH_SIZE / 3)
#define RX_RECOVERY_SIZE         (RX_FLASH_SIZE / 8)
#define RX_BOOTCTL_ADDR          (RX_FLASH_BASE + RX_FLASH_SIZE - 0x2000)
#define RX_BOOTCTL_BACKUP_ADDR   (RX_FLASH_BASE + RX_FLASH_SIZE - 0x4000)
#define RX_LOG_ADDR              (RX_FLASH_BASE + RX_FLASH_SIZE - 0x6000)

#define RX_CPU_HZ                120000000
#define RX_BOARD_ID              0xF201

void board_rx_early_init(void);
const eos_board_ops_t *board_rx_get_ops(void);

#endif /* BOARD_RX_H */
