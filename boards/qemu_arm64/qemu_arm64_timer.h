// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

#ifndef QEMU_ARM64_TIMER_H
#define QEMU_ARM64_TIMER_H

#include <stdint.h>

static inline uint32_t qemu_arm64_counter_to_ms(uint64_t counter,
                                                uint32_t frequency)
{
    uint32_t milliseconds = (uint32_t)(counter / frequency) * 1000U;
    uint64_t remainder = counter % frequency;

    milliseconds += (uint32_t)((remainder * 1000U) / frequency);
    return milliseconds;
}

#endif /* QEMU_ARM64_TIMER_H */
