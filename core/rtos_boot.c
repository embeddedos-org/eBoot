// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file rtos_boot.c
 * @brief RTOS-specific boot: detect type, configure MPU, jump to RTOS entry
 */

#include "eos_rtos_boot.h"
#include "eos_mpu_boot.h"
#include "eos_hal.h"
#include <string.h>

#define RTOS_MAGIC_OFFSET    0x200
#define FREERTOS_MAGIC       0x46524545  /* "FREE" */
#define ZEPHYR_MAGIC         0x5A455048  /* "ZEPH" */
#define NUTTX_MAGIC          0x4E555458  /* "NUTX" */
#define EOS_MAGIC            0x454F5300  /* "EOS\0" */

eos_rtos_type_t eos_rtos_detect(uint32_t image_addr)
{
    uint32_t magic = 0;
    const void *ptr = (const void *)(uintptr_t)(image_addr + RTOS_MAGIC_OFFSET);
    memcpy(&magic, ptr, sizeof(magic));

    switch (magic) {
        case FREERTOS_MAGIC: return EOS_RTOS_FREERTOS;
        case ZEPHYR_MAGIC:   return EOS_RTOS_ZEPHYR;
        case NUTTX_MAGIC:    return EOS_RTOS_NUTTX;
        case EOS_MAGIC:      return EOS_RTOS_CUSTOM;
        default:             return EOS_RTOS_NONE;
    }
}

int eos_rtos_configure_mpu(const eos_rtos_boot_config_t *cfg)
{
    if (!cfg) return EOS_ERR_INVALID;

    switch (cfg->mpu_mode) {
        case EOS_MPU_DISABLED:
            /* MPU remains off — RTOS manages it */
            break;

        case EOS_MPU_PRIVILEGED: {
            /* Configure MPU for privileged-only access to flash/RAM */
            eos_mpu_ctx_t mpu;
            eos_mpu_init(&mpu);
            /* Flash — privileged read-only, executable, cacheable */
            eos_mpu_add_region(&mpu, cfg->entry_addr & 0xFFF00000, 0x200000,
                               EOS_MPU_PRIV_RO, true, true);
            /* RAM — privileged read-write, no execute, cacheable */
            eos_mpu_add_region(&mpu, cfg->stack_addr & 0xFFF00000, 0x100000,
                               EOS_MPU_PRIV_RW, false, true);
            /* Peripherals — privileged RW, no execute, no cache */
            eos_mpu_add_region(&mpu, 0x40000000, 0x20000000,
                               EOS_MPU_PRIV_RW, false, false);
            /* System control — privileged only */
            eos_mpu_add_region(&mpu, 0xE0000000, 0x10000,
                               EOS_MPU_PRIV_RW, false, false);
            eos_mpu_apply(&mpu);
            break;
        }

        case EOS_MPU_PROTECTED: {
            /* Full MPU setup with separate regions for kernel/task isolation */
            eos_mpu_ctx_t mpu;
            eos_mpu_init(&mpu);
            /* RTOS kernel code — privileged read-only, executable */
            eos_mpu_add_region(&mpu, cfg->entry_addr & 0xFFF00000, 0x100000,
                               EOS_MPU_PRIV_RO, true, true);
            /* RTOS kernel data / heap — privileged RW, no execute */
            eos_mpu_add_region(&mpu, cfg->heap_start, cfg->heap_size,
                               EOS_MPU_PRIV_RW, false, true);
            /* Task stacks — unprivileged RW, no execute */
            eos_mpu_add_region(&mpu, cfg->stack_addr & 0xFFF00000, 0x40000,
                               EOS_MPU_FULL_RW, false, true);
            /* Shared memory — full RW, no execute */
            eos_mpu_add_region(&mpu, (cfg->stack_addr & 0xFFF00000) + 0x40000, 0x10000,
                               EOS_MPU_FULL_RW, false, true);
            /* Peripherals — privileged only, no execute, no cache */
            eos_mpu_add_region(&mpu, 0x40000000, 0x20000000,
                               EOS_MPU_PRIV_RW, false, false);
            /* System control — privileged only */
            eos_mpu_add_region(&mpu, 0xE0000000, 0x10000,
                               EOS_MPU_PRIV_RW, false, false);
            eos_mpu_apply(&mpu);
            break;
        }

        default:
            return EOS_ERR_INVALID;
    }

    return EOS_OK;
}

int eos_rtos_boot(const eos_rtos_boot_config_t *cfg)
{
    if (!cfg) return EOS_ERR_INVALID;
    if (cfg->entry_addr == 0 || cfg->stack_addr == 0) return EOS_ERR_INVALID;

    /* Configure MPU before handing off */
    int rc = eos_rtos_configure_mpu(cfg);
    if (rc != EOS_OK) return rc;

    /* Prepare boot parameters in a known location for the RTOS */
    extern int eos_rtos_params_store(const eos_rtos_boot_config_t *cfg);
    eos_rtos_params_store(cfg);

    /* Clean up bootloader state */
    eos_hal_disable_irqs();
    eos_hal_deinit_peripherals();

    /* Set stack pointer and jump */
    eos_hal_set_msp(cfg->stack_addr);
    eos_hal_jump(cfg->entry_addr);

    /* Should never reach here */
    return EOS_ERR_GENERIC;
}

const char *eos_rtos_type_name(eos_rtos_type_t type)
{
    switch (type) {
        case EOS_RTOS_FREERTOS: return "FreeRTOS";
        case EOS_RTOS_ZEPHYR:   return "Zephyr";
        case EOS_RTOS_NUTTX:    return "NuttX";
        case EOS_RTOS_RTTHREAD: return "RT-Thread";
        case EOS_RTOS_CUSTOM:   return "Custom";
        default:                return "Unknown";
    }
}
