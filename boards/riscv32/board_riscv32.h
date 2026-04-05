// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_riscv32.h
 * @brief RISC-V 32 ESP32-C3 board configuration
 */

#ifndef BOARD_RISCV32_H
#define BOARD_RISCV32_H

#include "eos_hal.h"

#define RISCV32_FLASH_BASE            0x00000000
#define RISCV32_FLASH_SIZE            (4*1024*1024)
#define RISCV32_RAM_BASE              0x3FC80000
#define RISCV32_RAM_SIZE              (400*1024)

#define RISCV32_SLOT_A_ADDR           (RISCV32_FLASH_BASE + 0x20000)
#define RISCV32_SLOT_A_SIZE           (RISCV32_FLASH_SIZE / 3)
#define RISCV32_SLOT_B_ADDR           (RISCV32_FLASH_BASE + RISCV32_FLASH_SIZE / 3 + 0x20000)
#define RISCV32_SLOT_B_SIZE           (RISCV32_FLASH_SIZE / 3)
#define RISCV32_RECOVERY_ADDR         (RISCV32_FLASH_BASE + 2 * RISCV32_FLASH_SIZE / 3)
#define RISCV32_RECOVERY_SIZE         (RISCV32_FLASH_SIZE / 8)
#define RISCV32_BOOTCTL_ADDR          (RISCV32_FLASH_BASE + RISCV32_FLASH_SIZE - 0x2000)
#define RISCV32_BOOTCTL_BACKUP_ADDR   (RISCV32_FLASH_BASE + RISCV32_FLASH_SIZE - 0x4000)
#define RISCV32_LOG_ADDR              (RISCV32_FLASH_BASE + RISCV32_FLASH_SIZE - 0x6000)

#define RISCV32_CPU_HZ                160000000
#define RISCV32_BOARD_ID              0x0101

void board_riscv32_early_init(void);
const eos_board_ops_t *board_riscv32_get_ops(void);

#endif /* BOARD_RISCV32_H */
