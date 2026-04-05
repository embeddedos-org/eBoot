// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_esp32s3.h
 * @brief ESP32-S3 Xtensa board configuration
 */

#ifndef BOARD_ESP32S3_H
#define BOARD_ESP32S3_H

#include "eos_hal.h"

#define ESP32S3_FLASH_BASE            0x00000000
#define ESP32S3_FLASH_SIZE            (16*1024*1024)
#define ESP32S3_RAM_BASE              0x3FC88000
#define ESP32S3_RAM_SIZE              (512*1024)

#define ESP32S3_SLOT_A_ADDR           (ESP32S3_FLASH_BASE + 0x20000)
#define ESP32S3_SLOT_A_SIZE           (ESP32S3_FLASH_SIZE / 3)
#define ESP32S3_SLOT_B_ADDR           (ESP32S3_FLASH_BASE + ESP32S3_FLASH_SIZE / 3 + 0x20000)
#define ESP32S3_SLOT_B_SIZE           (ESP32S3_FLASH_SIZE / 3)
#define ESP32S3_RECOVERY_ADDR         (ESP32S3_FLASH_BASE + 2 * ESP32S3_FLASH_SIZE / 3)
#define ESP32S3_RECOVERY_SIZE         (ESP32S3_FLASH_SIZE / 8)
#define ESP32S3_BOOTCTL_ADDR          (ESP32S3_FLASH_BASE + ESP32S3_FLASH_SIZE - 0x2000)
#define ESP32S3_BOOTCTL_BACKUP_ADDR   (ESP32S3_FLASH_BASE + ESP32S3_FLASH_SIZE - 0x4000)
#define ESP32S3_LOG_ADDR              (ESP32S3_FLASH_BASE + ESP32S3_FLASH_SIZE - 0x6000)

#define ESP32S3_CPU_HZ                240000000
#define ESP32S3_BOARD_ID              0x0201

void board_esp32s3_early_init(void);
const eos_board_ops_t *board_esp32s3_get_ops(void);

#endif /* BOARD_ESP32S3_H */
