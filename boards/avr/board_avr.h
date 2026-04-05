// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_avr.h
 * @brief ATmega328P AVR board configuration
 */

#ifndef BOARD_AVR_H
#define BOARD_AVR_H

#include "eos_hal.h"

#define AVR_FLASH_BASE            0x0000
#define AVR_FLASH_SIZE            (32*1024)
#define AVR_RAM_BASE              0x0100
#define AVR_RAM_SIZE              (2*1024)

#define AVR_SLOT_A_ADDR           (AVR_FLASH_BASE + 0x20000)
#define AVR_SLOT_A_SIZE           (AVR_FLASH_SIZE / 3)
#define AVR_SLOT_B_ADDR           (AVR_FLASH_BASE + AVR_FLASH_SIZE / 3 + 0x20000)
#define AVR_SLOT_B_SIZE           (AVR_FLASH_SIZE / 3)
#define AVR_RECOVERY_ADDR         (AVR_FLASH_BASE + 2 * AVR_FLASH_SIZE / 3)
#define AVR_RECOVERY_SIZE         (AVR_FLASH_SIZE / 8)
#define AVR_BOOTCTL_ADDR          (AVR_FLASH_BASE + AVR_FLASH_SIZE - 0x2000)
#define AVR_BOOTCTL_BACKUP_ADDR   (AVR_FLASH_BASE + AVR_FLASH_SIZE - 0x4000)
#define AVR_LOG_ADDR              (AVR_FLASH_BASE + AVR_FLASH_SIZE - 0x6000)

#define AVR_CPU_HZ                16000000
#define AVR_BOARD_ID              0xD101

void board_avr_early_init(void);
const eos_board_ops_t *board_avr_get_ops(void);

#endif /* BOARD_AVR_H */
