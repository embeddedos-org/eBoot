// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_xtensa_hifi.h
 * @brief Xtensa HiFi5 board configuration
 */

#ifndef BOARD_XTENSA_HIFI_H
#define BOARD_XTENSA_HIFI_H

#include "eos_hal.h"

#define XTENSA_HIFI_FLASH_BASE            0x00000000
#define XTENSA_HIFI_FLASH_SIZE            (4*1024*1024)
#define XTENSA_HIFI_RAM_BASE              0x00000000
#define XTENSA_HIFI_RAM_SIZE              (4*1024*1024)

#define XTENSA_HIFI_SLOT_A_ADDR           (XTENSA_HIFI_FLASH_BASE + 0x20000)
#define XTENSA_HIFI_SLOT_A_SIZE           (XTENSA_HIFI_FLASH_SIZE / 3)
#define XTENSA_HIFI_SLOT_B_ADDR           (XTENSA_HIFI_FLASH_BASE + XTENSA_HIFI_FLASH_SIZE / 3 + 0x20000)
#define XTENSA_HIFI_SLOT_B_SIZE           (XTENSA_HIFI_FLASH_SIZE / 3)
#define XTENSA_HIFI_RECOVERY_ADDR         (XTENSA_HIFI_FLASH_BASE + 2 * XTENSA_HIFI_FLASH_SIZE / 3)
#define XTENSA_HIFI_RECOVERY_SIZE         (XTENSA_HIFI_FLASH_SIZE / 8)
#define XTENSA_HIFI_BOOTCTL_ADDR          (XTENSA_HIFI_FLASH_BASE + XTENSA_HIFI_FLASH_SIZE - 0x2000)
#define XTENSA_HIFI_BOOTCTL_BACKUP_ADDR   (XTENSA_HIFI_FLASH_BASE + XTENSA_HIFI_FLASH_SIZE - 0x4000)
#define XTENSA_HIFI_LOG_ADDR              (XTENSA_HIFI_FLASH_BASE + XTENSA_HIFI_FLASH_SIZE - 0x6000)

#define XTENSA_HIFI_CPU_HZ                800000000
#define XTENSA_HIFI_BOARD_ID              0xFD01

void board_xtensa_hifi_early_init(void);
const eos_board_ops_t *board_xtensa_hifi_get_ops(void);

#endif /* BOARD_XTENSA_HIFI_H */
