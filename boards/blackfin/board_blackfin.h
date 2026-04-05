// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_blackfin.h
 * @brief Blackfin ADSP-BF537 board configuration
 */

#ifndef BOARD_BLACKFIN_H
#define BOARD_BLACKFIN_H

#include "eos_hal.h"

#define BLACKFIN_FLASH_BASE            0x20000000
#define BLACKFIN_FLASH_SIZE            (4*1024*1024)
#define BLACKFIN_RAM_BASE              0x00000000
#define BLACKFIN_RAM_SIZE              (128*1024*1024)

#define BLACKFIN_SLOT_A_ADDR           (BLACKFIN_FLASH_BASE + 0x20000)
#define BLACKFIN_SLOT_A_SIZE           (BLACKFIN_FLASH_SIZE / 3)
#define BLACKFIN_SLOT_B_ADDR           (BLACKFIN_FLASH_BASE + BLACKFIN_FLASH_SIZE / 3 + 0x20000)
#define BLACKFIN_SLOT_B_SIZE           (BLACKFIN_FLASH_SIZE / 3)
#define BLACKFIN_RECOVERY_ADDR         (BLACKFIN_FLASH_BASE + 2 * BLACKFIN_FLASH_SIZE / 3)
#define BLACKFIN_RECOVERY_SIZE         (BLACKFIN_FLASH_SIZE / 8)
#define BLACKFIN_BOOTCTL_ADDR          (BLACKFIN_FLASH_BASE + BLACKFIN_FLASH_SIZE - 0x2000)
#define BLACKFIN_BOOTCTL_BACKUP_ADDR   (BLACKFIN_FLASH_BASE + BLACKFIN_FLASH_SIZE - 0x4000)
#define BLACKFIN_LOG_ADDR              (BLACKFIN_FLASH_BASE + BLACKFIN_FLASH_SIZE - 0x6000)

#define BLACKFIN_CPU_HZ                600000000
#define BLACKFIN_BOARD_ID              0xF901

void board_blackfin_early_init(void);
const eos_board_ops_t *board_blackfin_get_ops(void);

#endif /* BOARD_BLACKFIN_H */
