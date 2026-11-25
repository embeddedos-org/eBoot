// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_x86_flash_range.c
 * @brief x86: the flash range check, and the map it checks against (#196).
 *
 * `board_x86.c` declared `X86_FLASH_BASE 0xFFF00000` with a 16 MiB size.
 * Two things followed, and this suite pins both:
 *
 *  1. Every region the bootloader manages -- slot A, slot B, the recovery
 *     image, the boot-control block and its backup, the log -- sat *below*
 *     that base, so `addr < X86_FLASH_BASE` refused all of them. The port has
 *     real flash read, write and erase ops, so this was not latent: read,
 *     write and erase of every managed region returned EOS_ERR_INVALID.
 *  2. The upper bound was `X86_FLASH_BASE + X86_FLASH_SIZE`, computed in the
 *     32 bits of the operand types. With the base now correct at 0xFF000000
 *     that sum is 0x100000000, which is 0 in 32 bits -- so writing the check
 *     that way would have refused every address in the device for the
 *     opposite reason. The check is a subtraction now and cannot wrap.
 *
 * The suite reaches `x86_flash_in_range()` by including the board file: it is
 * file-static, and the point is to test the predicate in both directions.
 * Going through the ops table can only show the refusals -- an accepted
 * address makes the driver dereference real memory-mapped NOR flash, which a
 * host does not have. The accept side is where the #196 defect lived, so it
 * is the side that has to be observable.
 */

/* board_x86.c, not a model of it. */
#include "../../boards/x86/board_x86.c"

#include <stdio.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

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

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL]\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

#define FLASH_END_EXCLUSIVE ((uint64_t)X86_FLASH_BASE + X86_FLASH_SIZE)

/* ---- the declared map ---------------------------------------------- */

TEST(the_declared_flash_ends_exactly_at_the_top_of_the_address_space)
{
    /* 0xFF000000 + 16 MiB == 0x100000000: the last byte of flash is the last
     * byte of a 32-bit address space, and not one byte further. At the old
     * base of 0xFFF00000 this was 0x10FF00000 -- 15 MiB of declared flash
     * that cannot be addressed. */
    ASSERT(FLASH_END_EXCLUSIVE == 0x100000000ULL);
    ASSERT(X86_FLASH_BASE == 0xFF000000u);
    ASSERT(X86_FLASH_SIZE == 16u * 1024u * 1024u);
}

TEST(every_region_the_board_declares_lies_inside_the_declared_flash)
{
    const eos_board_ops_t *ops = board_x86_get_ops();

    ASSERT(ops->flash_base == X86_FLASH_BASE);
    ASSERT(ops->flash_size == X86_FLASH_SIZE);

    const struct { uint32_t addr; uint32_t size; } regions[] = {
        { ops->slot_a_addr,   ops->slot_a_size   },
        { ops->slot_b_addr,   ops->slot_b_size   },
        { ops->recovery_addr, ops->recovery_size },
    };
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        ASSERT(regions[i].size != 0);
        ASSERT(regions[i].addr >= X86_FLASH_BASE);
        ASSERT((uint64_t)regions[i].addr + regions[i].size
               <= FLASH_END_EXCLUSIVE);
    }

    const uint32_t blocks[] = {
        ops->bootctl_addr, ops->bootctl_backup_addr, ops->log_addr,
    };
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        ASSERT(blocks[i] >= X86_FLASH_BASE);
        ASSERT((uint64_t)blocks[i] < FLASH_END_EXCLUSIVE);
    }
}

TEST(no_two_declared_regions_share_a_byte)
{
    const eos_board_ops_t *ops = board_x86_get_ops();
    const struct { uint32_t addr; uint32_t size; } regions[] = {
        { ops->slot_a_addr,   ops->slot_a_size   },
        { ops->slot_b_addr,   ops->slot_b_size   },
        { ops->recovery_addr, ops->recovery_size },
    };
    const size_t count = sizeof(regions) / sizeof(regions[0]);

    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            ASSERT(regions[i].addr >= regions[j].addr + regions[j].size
                   || regions[j].addr >= regions[i].addr + regions[i].size);
        }
    }

    /* And no block sits inside a region: a boot-control save must not land
     * in a slot, and a slot image must not reach the boot-control block. */
    const uint32_t blocks[] = {
        ops->bootctl_addr, ops->bootctl_backup_addr, ops->log_addr,
    };
    for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
        for (size_t i = 0; i < count; i++) {
            ASSERT(blocks[b] < regions[i].addr
                   || blocks[b] >= regions[i].addr + regions[i].size);
        }
    }

    /* The three blocks are three copies, not one address with three names. */
    ASSERT(ops->bootctl_addr != ops->bootctl_backup_addr);
    ASSERT(ops->bootctl_addr != ops->log_addr);
    ASSERT(ops->bootctl_backup_addr != ops->log_addr);
}

/* ---- the range check ------------------------------------------------ */

TEST(the_address_of_every_managed_region_is_accepted)
{
    /* This is the #196 defect itself: at base 0xFFF00000 every one of these
     * was below the declared base and refused, so the driver could not read,
     * write or erase anything the bootloader manages. */
    const eos_board_ops_t *ops = board_x86_get_ops();

    ASSERT(x86_flash_in_range(ops->slot_a_addr, 1));
    ASSERT(x86_flash_in_range(ops->slot_a_addr, ops->slot_a_size));
    ASSERT(x86_flash_in_range(ops->slot_b_addr, 1));
    ASSERT(x86_flash_in_range(ops->slot_b_addr, ops->slot_b_size));
    ASSERT(x86_flash_in_range(ops->recovery_addr, 1));
    ASSERT(x86_flash_in_range(ops->recovery_addr, ops->recovery_size));
    ASSERT(x86_flash_in_range(ops->bootctl_addr, 0x1000));
    ASSERT(x86_flash_in_range(ops->bootctl_backup_addr, 0x1000));
    ASSERT(x86_flash_in_range(ops->log_addr, 0x1000));
}

TEST(the_bounds_are_the_declared_flash_and_the_upper_one_does_not_wrap)
{
    ASSERT(x86_flash_in_range(X86_FLASH_BASE, 1));
    ASSERT(!x86_flash_in_range(X86_FLASH_BASE - 1, 1));
    ASSERT(!x86_flash_in_range(0, 1));

    /* The last byte of flash is the last byte of the address space. One byte
     * fits there; two do not. Written as `addr + len > base + size` both of
     * these wrap and the answers invert. */
    ASSERT(x86_flash_in_range(0xFFFFFFFFu, 1));
    ASSERT(!x86_flash_in_range(0xFFFFFFFFu, 2));
    ASSERT(x86_flash_in_range(0xFFFFF000u, 0x1000));
    ASSERT(!x86_flash_in_range(0xFFFFF000u, 0x1001));

    /* A whole-device range fits exactly; one byte more does not. */
    ASSERT(x86_flash_in_range(X86_FLASH_BASE, X86_FLASH_SIZE));
    ASSERT(!x86_flash_in_range(X86_FLASH_BASE, (size_t)X86_FLASH_SIZE + 1));

    /* A length large enough to wrap addr + len back into flash is refused. */
    ASSERT(!x86_flash_in_range(0xFFFF0000u, 0x20000));
    ASSERT(!x86_flash_in_range(X86_FLASH_BASE, SIZE_MAX));

    /* A zero-length range at an address in flash is accepted, as before;
     * one past the end is not. */
    ASSERT(x86_flash_in_range(X86_FLASH_BASE, 0));
    ASSERT(x86_flash_in_range(0xFFFFFFFFu, 0));
}

TEST(the_ops_refuse_an_address_outside_the_declared_flash)
{
    /* Through the ops table, so the refusal the callers see is pinned too.
     * Only the refusals: an accepted address dereferences real NOR flash. */
    const eos_board_ops_t *ops = board_x86_get_ops();
    uint8_t buf[4] = {0};

    ASSERT(ops->flash_read != NULL);
    ASSERT(ops->flash_write != NULL);
    ASSERT(ops->flash_erase != NULL);

    ASSERT(ops->flash_read(0x1000u, buf, sizeof(buf)) == EOS_ERR_INVALID);
    ASSERT(ops->flash_read(X86_FLASH_BASE - 1, buf, 1) == EOS_ERR_INVALID);
    ASSERT(ops->flash_read(0xFFFFFFFFu, buf, 2) == EOS_ERR_INVALID);
    ASSERT(ops->flash_read(X86_FLASH_BASE, NULL, 1) == EOS_ERR_INVALID);

    ASSERT(ops->flash_write(0x1000u, buf, sizeof(buf)) == EOS_ERR_INVALID);
    ASSERT(ops->flash_write(0xFFFFFFFFu, buf, 2) == EOS_ERR_INVALID);
    ASSERT(ops->flash_write(X86_FLASH_BASE, NULL, 1) == EOS_ERR_INVALID);

    ASSERT(ops->flash_erase(0x1000u, 0x1000) == EOS_ERR_INVALID);
    ASSERT(ops->flash_erase(0xFFFFFFFFu, 2) == EOS_ERR_INVALID);
}

int main(void)
{
    printf("\n=== x86 flash range and memory map (#196) ===\n");
    run_the_declared_flash_ends_exactly_at_the_top_of_the_address_space();
    run_every_region_the_board_declares_lies_inside_the_declared_flash();
    run_no_two_declared_regions_share_a_byte();
    run_the_address_of_every_managed_region_is_accepted();
    run_the_bounds_are_the_declared_flash_and_the_upper_one_does_not_wrap();
    run_the_ops_refuse_an_address_outside_the_declared_flash();
    printf("\n%d/%d tests passed\n\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
