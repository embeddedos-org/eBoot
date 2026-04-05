// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arm11.h
 * @brief BCM2835 ARM1176JZF-S board configuration
 */

#ifndef BOARD_ARM11_H
#define BOARD_ARM11_H

#include "eos_hal.h"

#define ARM11_FLASH_BASE            0x00000000
#define ARM11_FLASH_SIZE            (4*1024*1024)
#define ARM11_RAM_BASE              0x00000000
#define ARM11_RAM_SIZE              (512*1024*1024)

#define ARM11_SLOT_A_ADDR           (ARM11_FLASH_BASE + 0x20000)
#define ARM11_SLOT_A_SIZE           (ARM11_FLASH_SIZE / 3)
#define ARM11_SLOT_B_ADDR           (ARM11_FLASH_BASE + ARM11_FLASH_SIZE / 3 + 0x20000)
#define ARM11_SLOT_B_SIZE           (ARM11_FLASH_SIZE / 3)
#define ARM11_RECOVERY_ADDR         (ARM11_FLASH_BASE + 2 * ARM11_FLASH_SIZE / 3)
#define ARM11_RECOVERY_SIZE         (ARM11_FLASH_SIZE / 8)
#define ARM11_BOOTCTL_ADDR          (ARM11_FLASH_BASE + ARM11_FLASH_SIZE - 0x2000)
#define ARM11_BOOTCTL_BACKUP_ADDR   (ARM11_FLASH_BASE + ARM11_FLASH_SIZE - 0x4000)
#define ARM11_LOG_ADDR              (ARM11_FLASH_BASE + ARM11_FLASH_SIZE - 0x6000)

#define ARM11_CPU_HZ                700000000
#define ARM11_BOARD_ID              0xCB01

void board_arm11_early_init(void);
const eos_board_ops_t *board_arm11_get_ops(void);

#endif /* BOARD_ARM11_H */
