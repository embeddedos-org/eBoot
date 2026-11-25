// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_x86.h
 * @brief x86 board configuration (i486/Pentium, 16MB flash, 64MB RAM)
 */

#ifndef BOARD_X86_H
#define BOARD_X86_H

#include "eos_hal.h"

/* 0xFF000000, not 0xFFF00000 (#196). At 0xFFF00000 only 1 MiB of the declared
 * 16 MiB fits below 4 GiB -- 0xFFF00000 + 16 MiB is 0x10FF00000 -- and every
 * region named below sat *under* the declared base, so the driver's bounds
 * check refused every read, write and erase of every region the bootloader
 * manages. Each of those regions, and both boot-control copies, lies inside
 * a 16 MiB device at the top of the address space, which is what this base
 * now says:
 *
 *   (bootloader)  0xFF000000 - 0xFF040000    256 KiB
 *   slot A        0xFF040000 - 0xFF640000      6 MiB
 *   slot B        0xFF640000 - 0xFFC40000      6 MiB
 *   recovery      0xFFC40000 - 0xFFDC0000   1536 KiB
 *   log           0xFFEFD000                   (page)
 *   bootctl bkp   0xFFEFE000                   (page)
 *   bootctl       0xFFEFF000                   (page)
 *
 * Behaviour change: the JEDEC unlock addresses the driver derives from the
 * base move with it, from 0xFFF05555/0xFFF02AAA to 0xFF005555/0xFF002AAA.
 * That is the correct pair for a device at this base; the old pair addressed
 * a device that was not there. No x86 board has been run on hardware as far
 * as this repository shows. */
#define X86_FLASH_BASE            0xFF000000u
#define X86_FLASH_SIZE            (16u * 1024u * 1024u)
#define X86_RAM_BASE              0x00100000
#define X86_RAM_SIZE              (64 * 1024 * 1024)

#define X86_SLOT_A_ADDR           0xFF040000
#define X86_SLOT_A_SIZE           (6 * 1024 * 1024)
#define X86_SLOT_B_ADDR           0xFF640000
#define X86_SLOT_B_SIZE           (6 * 1024 * 1024)
#define X86_RECOVERY_ADDR         0xFFC40000
#define X86_RECOVERY_SIZE         (1536 * 1024)
#define X86_BOOTCTL_ADDR          0xFFEFF000
#define X86_BOOTCTL_BACKUP_ADDR   0xFFEFE000
#define X86_LOG_ADDR              0xFFEFD000

#define X86_CPU_HZ                100000000
#define X86_BOARD_ID              0x0E01

void board_x86_early_init(void);
const eos_board_ops_t *board_x86_get_ops(void);

#endif /* BOARD_X86_H */
