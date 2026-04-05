// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_microblaze.h
 * @brief MicroBlaze Xilinx board configuration
 */

#ifndef BOARD_MICROBLAZE_H
#define BOARD_MICROBLAZE_H

#include "eos_hal.h"

#define MICROBLAZE_FLASH_BASE            0x00000000
#define MICROBLAZE_FLASH_SIZE            (16*1024*1024)
#define MICROBLAZE_RAM_BASE              0x80000000
#define MICROBLAZE_RAM_SIZE              (128*1024*1024)

#define MICROBLAZE_SLOT_A_ADDR           (MICROBLAZE_FLASH_BASE + 0x20000)
#define MICROBLAZE_SLOT_A_SIZE           (MICROBLAZE_FLASH_SIZE / 3)
#define MICROBLAZE_SLOT_B_ADDR           (MICROBLAZE_FLASH_BASE + MICROBLAZE_FLASH_SIZE / 3 + 0x20000)
#define MICROBLAZE_SLOT_B_SIZE           (MICROBLAZE_FLASH_SIZE / 3)
#define MICROBLAZE_RECOVERY_ADDR         (MICROBLAZE_FLASH_BASE + 2 * MICROBLAZE_FLASH_SIZE / 3)
#define MICROBLAZE_RECOVERY_SIZE         (MICROBLAZE_FLASH_SIZE / 8)
#define MICROBLAZE_BOOTCTL_ADDR          (MICROBLAZE_FLASH_BASE + MICROBLAZE_FLASH_SIZE - 0x2000)
#define MICROBLAZE_BOOTCTL_BACKUP_ADDR   (MICROBLAZE_FLASH_BASE + MICROBLAZE_FLASH_SIZE - 0x4000)
#define MICROBLAZE_LOG_ADDR              (MICROBLAZE_FLASH_BASE + MICROBLAZE_FLASH_SIZE - 0x6000)

#define MICROBLAZE_CPU_HZ                200000000
#define MICROBLAZE_BOARD_ID              0xF501

void board_microblaze_early_init(void);
const eos_board_ops_t *board_microblaze_get_ops(void);

#endif /* BOARD_MICROBLAZE_H */
