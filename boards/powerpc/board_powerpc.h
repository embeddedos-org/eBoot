// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_powerpc.h
 * @brief PowerPC MPC8540 (e500) board configuration (e500 core, 32MB flash, 256MB RAM)
 */

#ifndef BOARD_POWERPC_H
#define BOARD_POWERPC_H

#include "eos_hal.h"

/* 16 MiB, not 32 (#197). 0xFF000000 + 32 MiB is 0x101000000: the declared
 * flash ended 16 MiB past the 4 GiB address space, and any range check
 * written as `addr + len <= flash_base + flash_size` computes that in 32 bits,
 * where it wraps to 0x01000000 and refuses every address in the device.
 * Only 16 MiB fits above this base, and every region below lies inside it:
 * slot A 0xFF100000, slot B 0xFF700000, recovery 0xFFD00000, boot control
 * 0xFFFFF000, its backup 0xFFFFE000 and the log 0xFFFFD000. A base of
 * 0xFE000000 with 32 MiB would also be coherent but leaves the lower half
 * named by no constant here. */
#define POWERPC_FLASH_BASE          0xFF000000u
#define POWERPC_FLASH_SIZE          (16u * 1024u * 1024u)
#define POWERPC_RAM_BASE            0x00000000
#define POWERPC_RAM_SIZE            (256 * 1024 * 1024)

#define POWERPC_SLOT_A_ADDR         0xFF100000
#define POWERPC_SLOT_A_SIZE         (6 * 1024 * 1024)
#define POWERPC_SLOT_B_ADDR         0xFF700000
#define POWERPC_SLOT_B_SIZE         (6 * 1024 * 1024)
#define POWERPC_RECOVERY_ADDR       0xFFD00000
#define POWERPC_RECOVERY_SIZE       (2 * 1024 * 1024)
#define POWERPC_BOOTCTL_ADDR        0xFFFFF000
#define POWERPC_BOOTCTL_BACKUP_ADDR 0xFFFFE000
#define POWERPC_LOG_ADDR            0xFFFFD000

#define POWERPC_CPU_HZ              800000000
#define POWERPC_BOARD_ID            0x1101

void board_powerpc_early_init(void);
const eos_board_ops_t *board_powerpc_get_ops(void);

#endif /* BOARD_POWERPC_H */
