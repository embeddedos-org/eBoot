// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_avr32.h
 * @brief AT32UC3A0512 AVR32 board configuration
 */

#ifndef BOARD_AVR32_H
#define BOARD_AVR32_H

#include "eos_hal.h"

#define AVR32_FLASH_BASE            0x80000000
#define AVR32_FLASH_SIZE            (512*1024)
#define AVR32_RAM_BASE              0x00000000
#define AVR32_RAM_SIZE              (64*1024)

#define AVR32_SLOT_A_ADDR           (AVR32_FLASH_BASE + 0x20000)
#define AVR32_SLOT_A_SIZE           (AVR32_FLASH_SIZE / 3)
#define AVR32_SLOT_B_ADDR           (AVR32_FLASH_BASE + AVR32_FLASH_SIZE / 3 + 0x20000)
#define AVR32_SLOT_B_SIZE           (AVR32_FLASH_SIZE / 3)
#define AVR32_RECOVERY_ADDR         (AVR32_FLASH_BASE + 2 * AVR32_FLASH_SIZE / 3)
#define AVR32_RECOVERY_SIZE         (AVR32_FLASH_SIZE / 8)
#define AVR32_BOOTCTL_ADDR          (AVR32_FLASH_BASE + AVR32_FLASH_SIZE - 0x2000)
#define AVR32_BOOTCTL_BACKUP_ADDR   (AVR32_FLASH_BASE + AVR32_FLASH_SIZE - 0x4000)
#define AVR32_LOG_ADDR              (AVR32_FLASH_BASE + AVR32_FLASH_SIZE - 0x6000)

#define AVR32_CPU_HZ                66000000
#define AVR32_BOARD_ID              0xD201

void board_avr32_early_init(void);
const eos_board_ops_t *board_avr32_get_ops(void);

#endif /* BOARD_AVR32_H */
