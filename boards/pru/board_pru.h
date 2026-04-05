// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_pru.h
 * @brief AM335x PRU-ICSS board configuration
 */

#ifndef BOARD_PRU_H
#define BOARD_PRU_H

#include "eos_hal.h"

#define PRU_FLASH_BASE            0x00000000
#define PRU_FLASH_SIZE            (8*1024)
#define PRU_RAM_BASE              0x00000000
#define PRU_RAM_SIZE              (16*1024)

#define PRU_SLOT_A_ADDR           (PRU_FLASH_BASE + 0x20000)
#define PRU_SLOT_A_SIZE           (PRU_FLASH_SIZE / 3)
#define PRU_SLOT_B_ADDR           (PRU_FLASH_BASE + PRU_FLASH_SIZE / 3 + 0x20000)
#define PRU_SLOT_B_SIZE           (PRU_FLASH_SIZE / 3)
#define PRU_RECOVERY_ADDR         (PRU_FLASH_BASE + 2 * PRU_FLASH_SIZE / 3)
#define PRU_RECOVERY_SIZE         (PRU_FLASH_SIZE / 8)
#define PRU_BOOTCTL_ADDR          (PRU_FLASH_BASE + PRU_FLASH_SIZE - 0x2000)
#define PRU_BOOTCTL_BACKUP_ADDR   (PRU_FLASH_BASE + PRU_FLASH_SIZE - 0x4000)
#define PRU_LOG_ADDR              (PRU_FLASH_BASE + PRU_FLASH_SIZE - 0x6000)

#define PRU_CPU_HZ                200000000
#define PRU_BOARD_ID              0xE401

void board_pru_early_init(void);
const eos_board_ops_t *board_pru_get_ops(void);

#endif /* BOARD_PRU_H */
