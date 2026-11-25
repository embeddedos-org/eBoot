// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_samd51.h
 * @brief Microchip SAMD51 board configuration (Cortex-M4F, 120MHz)
 *
 * Popular in hobbyist/industrial IoT — Arduino, Adafruit, and custom PCBs.
 */

#ifndef BOARD_SAMD51_H
#define BOARD_SAMD51_H

#include "eos_hal.h"

#define SAMD51_FLASH_BASE           0x00000000
#define SAMD51_FLASH_SIZE           (512 * 1024)
#define SAMD51_RAM_BASE             0x20000000
#define SAMD51_RAM_SIZE             (192 * 1024)

/* 208 KiB slots, not 224 (#195). At 224 KiB slot B ran 0x48000-0x80000, to
 * the end of flash, so its last 16 KiB were also the recovery image and the
 * boot-control block: an image larger than 208 KiB in slot B would have
 * overwritten both, and a boot-control save would have written into slot B.
 * Every address is unchanged; the slots stop short of the recovery image
 * instead. Both slots stay the same size, so an image that fits one fits the
 * other. 0x44000-0x48000 is left unused rather than move slot B's address.
 *
 *   slot A    0x10000 - 0x44000   208 KiB
 *   (unused)  0x44000 - 0x48000    16 KiB
 *   slot B    0x48000 - 0x7C000   208 KiB
 *   recovery  0x7C000 - 0x7E000     8 KiB
 *   bootctl   0x7E000                 (last page)
 */
#define SAMD51_SLOT_A_ADDR          0x00010000
#define SAMD51_SLOT_A_SIZE          (208 * 1024)
#define SAMD51_SLOT_B_ADDR          0x00048000
#define SAMD51_SLOT_B_SIZE          (208 * 1024)
#define SAMD51_RECOVERY_ADDR        0x0007C000
#define SAMD51_RECOVERY_SIZE        (8 * 1024)
#define SAMD51_BOOTCTL_ADDR         0x0007E000

#define SAMD51_CPU_HZ               120000000
#define SAMD51_BOARD_ID              0xD51A
#define SAMD51_PLATFORM              EOS_PLATFORM_ARM_CM4

void board_samd51_early_init(void);
const eos_board_ops_t *board_samd51_get_ops(void);

#endif /* BOARD_SAMD51_H */
