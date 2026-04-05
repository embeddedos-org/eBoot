// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_s390.h
 * @brief IBM z15 board configuration
 */

#ifndef BOARD_S390_H
#define BOARD_S390_H

#include "eos_hal.h"

#define S390_FLASH_BASE            0x00000000
#define S390_FLASH_SIZE            (4*1024*1024)
#define S390_RAM_BASE              0x00000000
#define S390_RAM_SIZE              (524288UL*1024*1024)

#define S390_SLOT_A_ADDR           (S390_FLASH_BASE + 0x20000)
#define S390_SLOT_A_SIZE           (S390_FLASH_SIZE / 3)
#define S390_SLOT_B_ADDR           (S390_FLASH_BASE + S390_FLASH_SIZE / 3 + 0x20000)
#define S390_SLOT_B_SIZE           (S390_FLASH_SIZE / 3)
#define S390_RECOVERY_ADDR         (S390_FLASH_BASE + 2 * S390_FLASH_SIZE / 3)
#define S390_RECOVERY_SIZE         (S390_FLASH_SIZE / 8)
#define S390_BOOTCTL_ADDR          (S390_FLASH_BASE + S390_FLASH_SIZE - 0x2000)
#define S390_BOOTCTL_BACKUP_ADDR   (S390_FLASH_BASE + S390_FLASH_SIZE - 0x4000)
#define S390_LOG_ADDR              (S390_FLASH_BASE + S390_FLASH_SIZE - 0x6000)

#define S390_CPU_HZ                5200000000
#define S390_BOARD_ID              0x0B01

void board_s390_early_init(void);
const eos_board_ops_t *board_s390_get_ops(void);

#endif /* BOARD_S390_H */
