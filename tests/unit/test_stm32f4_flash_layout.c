// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_stm32f4_flash_layout.c
 * @brief STM32F4 erase covers the whole range; no two regions share a sector.
 *
 * Two defects, one consequence. stm32f4_flash_erase() erased only the
 * sector holding addr and ignored len, so a 448K slot erase cleared one
 * sector of it. And the memory map put Slot B's tail, the recovery image,
 * both boot control copies and the boot log in sector 11, so the erase
 * that eos_bootctl_save() issues before every write wiped the other four.
 *
 * board_stm32f4.c drives flash registers and is built only by a cross
 * build; its erase takes the sector range from stm32f4_flash_sector_range()
 * in board_stm32f4.h, which is plain C and is what this suite exercises,
 * together with the memory map macros in the same header.
 */

#include "board_stm32f4.h"
#include "eos_bootctl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("  %-58s ", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

#define FLASH_END  ((uint32_t)STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE)

/* RM0090 table 5: STM32F405/407 main memory sectors. */
static const uint32_t sector_start[STM32F4_FLASH_SECTORS + 1] = {
    0x08000000, 0x08004000, 0x08008000, 0x0800C000, 0x08010000, 0x08020000,
    0x08040000, 0x08060000, 0x08080000, 0x080A0000, 0x080C0000, 0x080E0000,
    0x08100000,
};

static int is_sector_boundary(uint32_t addr)
{
    for (int i = 0; i <= STM32F4_FLASH_SECTORS; i++)
        if (sector_start[i] == addr) return 1;
    return 0;
}

TEST(test_sector_of_matches_the_reference_manual)
{
    for (int i = 0; i < STM32F4_FLASH_SECTORS; i++) {
        ASSERT(stm32f4_flash_sector_of(sector_start[i]) == i);
        ASSERT(stm32f4_flash_sector_of(sector_start[i + 1] - 1) == i);
    }
    ASSERT(stm32f4_flash_sector_of(STM32F4_FLASH_BASE - 1) == -1);
    ASSERT(stm32f4_flash_sector_of(FLASH_END) == -1);
}

TEST(test_erase_range_covers_every_sector_it_touches)
{
    int first = -1, last = -1;

    /* A whole slot: both of its sectors, not just the first. */
    ASSERT(stm32f4_flash_sector_range(STM32F4_SLOT_A_ADDR, STM32F4_SLOT_A_SIZE,
                                      &first, &last) == 0);
    ASSERT(first == 5 && last == 6);
    ASSERT(stm32f4_flash_sector_range(STM32F4_SLOT_B_ADDR, STM32F4_SLOT_B_SIZE,
                                      &first, &last) == 0);
    ASSERT(first == 7 && last == 8);

    /* Spanning the 16K / 64K / 128K geometry changes. */
    ASSERT(stm32f4_flash_sector_range(0x0800C000, 0x14001, &first, &last) == 0);
    ASSERT(first == 3 && last == 5);

    /* The whole array. */
    ASSERT(stm32f4_flash_sector_range(STM32F4_FLASH_BASE, STM32F4_FLASH_SIZE,
                                      &first, &last) == 0);
    ASSERT(first == 0 && last == STM32F4_FLASH_SECTORS - 1);

    /* A small erase stays in its sector. */
    ASSERT(stm32f4_flash_sector_range(STM32F4_BOOTCTL_ADDR, sizeof(eos_bootctl_t),
                                      &first, &last) == 0);
    ASSERT(first == 10 && last == 10);
}

TEST(test_erase_range_rejects_what_it_cannot_erase)
{
    int first = 99, last = 99;

    ASSERT(stm32f4_flash_sector_range(STM32F4_SLOT_A_ADDR, 0, &first, &last) != 0);
    ASSERT(stm32f4_flash_sector_range(FLASH_END - 4, 8, &first, &last) != 0);
    ASSERT(stm32f4_flash_sector_range(STM32F4_FLASH_BASE - 4, 8, &first, &last) != 0);
    ASSERT(stm32f4_flash_sector_range(0xFFFFFF00u, 0x200, &first, &last) != 0);
    ASSERT(first == 99 && last == 99);
}

struct region { const char *name; uint32_t addr; uint32_t size; };

static const struct region regions[] = {
    { "stage0",         STM32F4_STAGE0_ADDR,         STM32F4_STAGE0_SIZE },
    { "stage1",         STM32F4_STAGE1_ADDR,         STM32F4_STAGE1_SIZE },
    { "log",            STM32F4_LOG_ADDR,            STM32F4_LOG_SIZE },
    { "slot_a",         STM32F4_SLOT_A_ADDR,         STM32F4_SLOT_A_SIZE },
    { "slot_b",         STM32F4_SLOT_B_ADDR,         STM32F4_SLOT_B_SIZE },
    { "recovery",       STM32F4_RECOVERY_ADDR,       STM32F4_RECOVERY_SIZE },
    { "bootctl",        STM32F4_BOOTCTL_ADDR,        STM32F4_BOOTCTL_SIZE },
    { "bootctl_backup", STM32F4_BOOTCTL_BACKUP_ADDR, STM32F4_BOOTCTL_BACKUP_SIZE },
};
#define N_REGIONS (sizeof(regions) / sizeof(regions[0]))

TEST(test_every_region_owns_whole_sectors)
{
    for (size_t i = 0; i < N_REGIONS; i++) {
        const struct region *r = &regions[i];
        if (!is_sector_boundary(r->addr) || !is_sector_boundary(r->addr + r->size)) {
            printf("\n    %s [0x%08X, +0x%X) is not sector-aligned ",
                   r->name, (unsigned)r->addr, (unsigned)r->size);
            ASSERT(0);
        }
    }
}

TEST(test_no_two_regions_share_a_sector)
{
    int owner[STM32F4_FLASH_SECTORS];
    for (int s = 0; s < STM32F4_FLASH_SECTORS; s++) owner[s] = -1;

    for (size_t i = 0; i < N_REGIONS; i++) {
        int first, last;
        ASSERT(stm32f4_flash_sector_range(regions[i].addr, regions[i].size,
                                          &first, &last) == 0);
        for (int s = first; s <= last; s++) {
            if (owner[s] != -1) {
                printf("\n    sector %d holds both %s and %s ", s,
                       regions[owner[s]].name, regions[i].name);
                ASSERT(0);
            }
            owner[s] = (int)i;
        }
    }
}

TEST(test_layout_fits_what_it_holds)
{
    /* A/B slots are interchangeable, so they are the same size. */
    ASSERT(STM32F4_SLOT_A_SIZE == STM32F4_SLOT_B_SIZE);
    ASSERT(sizeof(eos_bootctl_t) <= STM32F4_BOOTCTL_SIZE);
    ASSERT(sizeof(eos_bootctl_t) <= STM32F4_BOOTCTL_BACKUP_SIZE);
    ASSERT(EOS_BOOT_LOG_MAX * sizeof(eos_boot_log_entry_t) <= STM32F4_LOG_SIZE);
    /* Stage-1's vector table is where stage-0 jumps; the linker scripts
     * place stage-0 at 0x08000000/16K and stage-1 at 0x08004000/48K. */
    ASSERT(STM32F4_STAGE0_ADDR == 0x08000000 && STM32F4_STAGE0_SIZE == 16 * 1024);
    ASSERT(STM32F4_STAGE1_ADDR == 0x08004000 && STM32F4_STAGE1_SIZE == 48 * 1024);
}

int main(void)
{
    printf("STM32F4 flash erase and layout\n\n");

    run_test_sector_of_matches_the_reference_manual();
    run_test_erase_range_covers_every_sector_it_touches();
    run_test_erase_range_rejects_what_it_cannot_erase();
    run_test_every_region_owns_whole_sectors();
    run_test_no_two_regions_share_a_sector();
    run_test_layout_fits_what_it_holds();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
