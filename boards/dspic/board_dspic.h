// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_dspic.h
 * @brief dsPIC33FJ256GP710 board configuration
 */

#ifndef BOARD_DSPIC_H
#define BOARD_DSPIC_H

#include "eos_hal.h"

#define DSPIC_FLASH_BASE            0x000000
#define DSPIC_FLASH_SIZE            (256*1024)
#define DSPIC_RAM_BASE              0x000800
#define DSPIC_RAM_SIZE              (30*1024)

#define DSPIC_SLOT_A_ADDR           (DSPIC_FLASH_BASE + 0x20000)
#define DSPIC_SLOT_A_SIZE           (DSPIC_FLASH_SIZE / 3)
#define DSPIC_SLOT_B_ADDR           (DSPIC_FLASH_BASE + DSPIC_FLASH_SIZE / 3 + 0x20000)
#define DSPIC_SLOT_B_SIZE           (DSPIC_FLASH_SIZE / 3)
#define DSPIC_RECOVERY_ADDR         (DSPIC_FLASH_BASE + 2 * DSPIC_FLASH_SIZE / 3)
#define DSPIC_RECOVERY_SIZE         (DSPIC_FLASH_SIZE / 8)
#define DSPIC_BOOTCTL_ADDR          (DSPIC_FLASH_BASE + DSPIC_FLASH_SIZE - 0x2000)
#define DSPIC_BOOTCTL_BACKUP_ADDR   (DSPIC_FLASH_BASE + DSPIC_FLASH_SIZE - 0x4000)
#define DSPIC_LOG_ADDR              (DSPIC_FLASH_BASE + DSPIC_FLASH_SIZE - 0x6000)

#define DSPIC_CPU_HZ                40000000
#define DSPIC_BOARD_ID              0xD601

void board_dspic_early_init(void);
const eos_board_ops_t *board_dspic_get_ops(void);

#endif /* BOARD_DSPIC_H */
