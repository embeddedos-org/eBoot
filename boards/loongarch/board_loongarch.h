// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_loongarch.h
 * @brief Loongson 3A5000 board configuration
 */

#ifndef BOARD_LOONGARCH_H
#define BOARD_LOONGARCH_H

#include "eos_hal.h"

#define LOONGARCH_FLASH_BASE            0x00000000
#define LOONGARCH_FLASH_SIZE            (4*1024*1024)
#define LOONGARCH_RAM_BASE              0x00000000
#define LOONGARCH_RAM_SIZE              (16384UL*1024*1024)

#define LOONGARCH_SLOT_A_ADDR           (LOONGARCH_FLASH_BASE + 0x20000)
#define LOONGARCH_SLOT_A_SIZE           (LOONGARCH_FLASH_SIZE / 3)
#define LOONGARCH_SLOT_B_ADDR           (LOONGARCH_FLASH_BASE + LOONGARCH_FLASH_SIZE / 3 + 0x20000)
#define LOONGARCH_SLOT_B_SIZE           (LOONGARCH_FLASH_SIZE / 3)
#define LOONGARCH_RECOVERY_ADDR         (LOONGARCH_FLASH_BASE + 2 * LOONGARCH_FLASH_SIZE / 3)
#define LOONGARCH_RECOVERY_SIZE         (LOONGARCH_FLASH_SIZE / 8)
#define LOONGARCH_BOOTCTL_ADDR          (LOONGARCH_FLASH_BASE + LOONGARCH_FLASH_SIZE - 0x2000)
#define LOONGARCH_BOOTCTL_BACKUP_ADDR   (LOONGARCH_FLASH_BASE + LOONGARCH_FLASH_SIZE - 0x4000)
#define LOONGARCH_LOG_ADDR              (LOONGARCH_FLASH_BASE + LOONGARCH_FLASH_SIZE - 0x6000)

#define LOONGARCH_CPU_HZ                2500000000
#define LOONGARCH_BOARD_ID              0x0701

void board_loongarch_early_init(void);
const eos_board_ops_t *board_loongarch_get_ops(void);

#endif /* BOARD_LOONGARCH_H */
