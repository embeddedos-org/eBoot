// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arm9.h
 * @brief AT91SAM9G25 ARM926EJ-S board configuration
 */

#ifndef BOARD_ARM9_H
#define BOARD_ARM9_H

#include "eos_hal.h"

#define ARM9_FLASH_BASE            0x00000000
#define ARM9_FLASH_SIZE            (4*1024*1024)
#define ARM9_RAM_BASE              0x20000000
#define ARM9_RAM_SIZE              (128*1024*1024)

#define ARM9_SLOT_A_ADDR           (ARM9_FLASH_BASE + 0x20000)
#define ARM9_SLOT_A_SIZE           (ARM9_FLASH_SIZE / 3)
#define ARM9_SLOT_B_ADDR           (ARM9_FLASH_BASE + ARM9_FLASH_SIZE / 3 + 0x20000)
#define ARM9_SLOT_B_SIZE           (ARM9_FLASH_SIZE / 3)
#define ARM9_RECOVERY_ADDR         (ARM9_FLASH_BASE + 2 * ARM9_FLASH_SIZE / 3)
#define ARM9_RECOVERY_SIZE         (ARM9_FLASH_SIZE / 8)
#define ARM9_BOOTCTL_ADDR          (ARM9_FLASH_BASE + ARM9_FLASH_SIZE - 0x2000)
#define ARM9_BOOTCTL_BACKUP_ADDR   (ARM9_FLASH_BASE + ARM9_FLASH_SIZE - 0x4000)
#define ARM9_LOG_ADDR              (ARM9_FLASH_BASE + ARM9_FLASH_SIZE - 0x6000)

#define ARM9_CPU_HZ                400000000
#define ARM9_BOARD_ID              0xC901

void board_arm9_early_init(void);
const eos_board_ops_t *board_arm9_get_ops(void);

#endif /* BOARD_ARM9_H */
