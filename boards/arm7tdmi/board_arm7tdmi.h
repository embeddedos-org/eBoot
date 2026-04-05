// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_arm7tdmi.h
 * @brief LPC2148 ARM7TDMI board configuration
 */

#ifndef BOARD_ARM7TDMI_H
#define BOARD_ARM7TDMI_H

#include "eos_hal.h"

#define ARM7TDMI_FLASH_BASE            0x00000000
#define ARM7TDMI_FLASH_SIZE            (512*1024)
#define ARM7TDMI_RAM_BASE              0x40000000
#define ARM7TDMI_RAM_SIZE              (40*1024)

#define ARM7TDMI_SLOT_A_ADDR           (ARM7TDMI_FLASH_BASE + 0x20000)
#define ARM7TDMI_SLOT_A_SIZE           (ARM7TDMI_FLASH_SIZE / 3)
#define ARM7TDMI_SLOT_B_ADDR           (ARM7TDMI_FLASH_BASE + ARM7TDMI_FLASH_SIZE / 3 + 0x20000)
#define ARM7TDMI_SLOT_B_SIZE           (ARM7TDMI_FLASH_SIZE / 3)
#define ARM7TDMI_RECOVERY_ADDR         (ARM7TDMI_FLASH_BASE + 2 * ARM7TDMI_FLASH_SIZE / 3)
#define ARM7TDMI_RECOVERY_SIZE         (ARM7TDMI_FLASH_SIZE / 8)
#define ARM7TDMI_BOOTCTL_ADDR          (ARM7TDMI_FLASH_BASE + ARM7TDMI_FLASH_SIZE - 0x2000)
#define ARM7TDMI_BOOTCTL_BACKUP_ADDR   (ARM7TDMI_FLASH_BASE + ARM7TDMI_FLASH_SIZE - 0x4000)
#define ARM7TDMI_LOG_ADDR              (ARM7TDMI_FLASH_BASE + ARM7TDMI_FLASH_SIZE - 0x6000)

#define ARM7TDMI_CPU_HZ                60000000
#define ARM7TDMI_BOARD_ID              0xC701

void board_arm7tdmi_early_init(void);
const eos_board_ops_t *board_arm7tdmi_get_ops(void);

#endif /* BOARD_ARM7TDMI_H */
