// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file reset_entry.c
 * @brief Stage-1 (E-Boot) entry point
 *
 * Stage-0 hands over the way the core resets: it loads the stack pointer
 * from the first word of the stage-1 image and branches to the second
 * (boards/stm32f4/board_stm32f4.c, stm32f4_jump). So the image has to begin
 * with a vector table, and something has to own it. Nothing did: stage1/
 * exported eboot_main() only, the linker script had no ENTRY and kept no
 * section, and with -nostartfiles and --gc-sections the linker had nothing
 * to anchor -- eboot_firmware.bin was zero bytes, and stage-0 embedded the
 * SHA-256 of the empty string as the hash it verifies stage-1 against.
 *
 * Same shape as stage0/reset_entry.c: copy .data, zero .bss, enter the
 * stage's main. The table runs through SysTick, because board_early_init()
 * on stm32f4 enables the SysTick interrupt and a table that stops at
 * UsageFault has the core fetch that vector from whatever code follows.
 */

#include <stdint.h>

extern int eboot_main(void);

static void default_handler(void)
{
    while (1) {
        /* Halt on an unhandled fault */
    }
}

void NMI_Handler(void)        __attribute__((weak, alias("default_handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("default_handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("default_handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("default_handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("default_handler")));
void SVC_Handler(void)        __attribute__((weak, alias("default_handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("default_handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("default_handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("default_handler")));

/* Provided by the linker script */
extern uint32_t _estack;
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _sbss, _ebss;

/**
 * @brief Reset handler: the address stage-0 branches to.
 */
void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    /* cppcheck-suppress comparePointers  // _sdata and _edata are linker symbols bounding one section, not two C objects; this is the startup idiom and there is no standard-conforming way to write it */
    while (dst < &_edata)
        *dst++ = *src++;

    dst = &_sbss;
    /* cppcheck-suppress comparePointers  // same: _sbss and _ebss bound the .bss the linker script emitted */
    while (dst < &_ebss)
        *dst++ = 0;

    (void)eboot_main();

    /* eboot_main() ends in recovery or a jump; if it ever returns, stop. */
    while (1);
}

/**
 * @brief Vector table for stage-1: initial stack, reset, the core faults,
 * and the system exceptions through SysTick.
 */
__attribute__((section(".isr_vector"), used))
const uint32_t stage1_vector_table[] = {
    (uint32_t)&_estack,           /* Initial stack pointer */
    (uint32_t)Reset_Handler,       /* Reset */
    (uint32_t)NMI_Handler,         /* NMI */
    (uint32_t)HardFault_Handler,   /* Hard fault */
    (uint32_t)MemManage_Handler,   /* Memory management fault */
    (uint32_t)BusFault_Handler,    /* Bus fault */
    (uint32_t)UsageFault_Handler,  /* Usage fault */
    0, 0, 0, 0,                    /* Reserved */
    (uint32_t)SVC_Handler,         /* SVCall */
    (uint32_t)DebugMon_Handler,    /* Debug monitor */
    0,                             /* Reserved */
    (uint32_t)PendSV_Handler,      /* PendSV */
    (uint32_t)SysTick_Handler,     /* SysTick */
};
