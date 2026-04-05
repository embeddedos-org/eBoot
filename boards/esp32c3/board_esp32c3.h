// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_esp32c3.h
 * @brief ESP32-C3 RISC-V board configuration
 */

#ifndef BOARD_ESP32C3_H
#define BOARD_ESP32C3_H

#include "eos_hal.h"

#define ESP32C3_FLASH_BASE            0x00000000
#define ESP32C3_FLASH_SIZE            (4*1024*1024)
#define ESP32C3_RAM_BASE              0x3FC80000
#define ESP32C3_RAM_SIZE              (400*1024)

#define ESP32C3_SLOT_A_ADDR           (ESP32C3_FLASH_BASE + 0x20000)
#define ESP32C3_SLOT_A_SIZE           (ESP32C3_FLASH_SIZE / 3)
#define ESP32C3_SLOT_B_ADDR           (ESP32C3_FLASH_BASE + ESP32C3_FLASH_SIZE / 3 + 0x20000)
#define ESP32C3_SLOT_B_SIZE           (ESP32C3_FLASH_SIZE / 3)
#define ESP32C3_RECOVERY_ADDR         (ESP32C3_FLASH_BASE + 2 * ESP32C3_FLASH_SIZE / 3)
#define ESP32C3_RECOVERY_SIZE         (ESP32C3_FLASH_SIZE / 8)
#define ESP32C3_BOOTCTL_ADDR          (ESP32C3_FLASH_BASE + ESP32C3_FLASH_SIZE - 0x2000)
#define ESP32C3_BOOTCTL_BACKUP_ADDR   (ESP32C3_FLASH_BASE + ESP32C3_FLASH_SIZE - 0x4000)
#define ESP32C3_LOG_ADDR              (ESP32C3_FLASH_BASE + ESP32C3_FLASH_SIZE - 0x6000)

#define ESP32C3_CPU_HZ                160000000
#define ESP32C3_BOARD_ID              0x0301

void board_esp32c3_early_init(void);
const eos_board_ops_t *board_esp32c3_get_ops(void);

#endif /* BOARD_ESP32C3_H */
