// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_msp430.h
 * @brief MSP430FR5994 board configuration
 */

#ifndef BOARD_MSP430_H
#define BOARD_MSP430_H

#include "eos_hal.h"

#define MSP430_FLASH_BASE            0x00004400
#define MSP430_FLASH_SIZE            (256*1024)
#define MSP430_RAM_BASE              0x00001C00
#define MSP430_RAM_SIZE              (8*1024)

#define MSP430_SLOT_A_ADDR           (MSP430_FLASH_BASE + 0x20000)
#define MSP430_SLOT_A_SIZE           (MSP430_FLASH_SIZE / 3)
#define MSP430_SLOT_B_ADDR           (MSP430_FLASH_BASE + MSP430_FLASH_SIZE / 3 + 0x20000)
#define MSP430_SLOT_B_SIZE           (MSP430_FLASH_SIZE / 3)
#define MSP430_RECOVERY_ADDR         (MSP430_FLASH_BASE + 2 * MSP430_FLASH_SIZE / 3)
#define MSP430_RECOVERY_SIZE         (MSP430_FLASH_SIZE / 8)
#define MSP430_BOOTCTL_ADDR          (MSP430_FLASH_BASE + MSP430_FLASH_SIZE - 0x2000)
#define MSP430_BOOTCTL_BACKUP_ADDR   (MSP430_FLASH_BASE + MSP430_FLASH_SIZE - 0x4000)
#define MSP430_LOG_ADDR              (MSP430_FLASH_BASE + MSP430_FLASH_SIZE - 0x6000)

#define MSP430_CPU_HZ                16000000
#define MSP430_BOARD_ID              0xE101

void board_msp430_early_init(void);
const eos_board_ops_t *board_msp430_get_ops(void);

#endif /* BOARD_MSP430_H */
