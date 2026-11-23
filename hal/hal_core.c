// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file hal_core.c
 * @brief HAL core — board ops registration and convenience wrappers
 */

#include "eos_hal.h"

static const eos_board_ops_t *g_ops = NULL;

void eos_hal_init(const eos_board_ops_t *ops)
{
    g_ops = ops;
}

const eos_board_ops_t *eos_hal_get_ops(void)
{
    return g_ops;
}

int eos_hal_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!g_ops || !g_ops->flash_read)
        return EOS_ERR_GENERIC;
    return g_ops->flash_read(addr, buf, len);
}

/* ---- Bootloader-region allow-list ----

 * A board that declares its bootloader region (bootloader_size
 * non-zero -- the address itself may be 0, flash can live there)
 * gets every flash write and erase checked against it: any
 * range intersecting the region is refused with EOS_ERR_WRITE_PROTECTED
 * before the board driver is called, so no caller -- the updater, the
 * recovery transport, a future bootloader self-update -- can overwrite
 * stage-0 or stage-1 through the HAL. Reads are unaffected. A board
 * that declares no region keeps today's behaviour: the guard cannot
 * protect a region the board will not name. The check is overflow-safe:
 * the end is computed in 64 bits, and a range that wraps uint32_t is
 * checked twice -- once for its linear part, once for the tail that
 * re-enters at address 0 -- so a wrapped range whose tail reaches the
 * region cannot smuggle past the test. */
static bool flash_range_hits_bootloader(uint32_t addr, size_t len)
{
    /* Undeclared is bootloader_size == 0: a region at address 0 is a
     * real layout, and the old bootloader_addr == 0 test silently
     * unguarded it -- declared-but-unprotected, fail-open. */
    if (!g_ops || g_ops->bootloader_size == 0)
        return false;
    /* A range at least as long as the whole 32-bit address space
     * covers every address, so it hits any declared region. On a
     * 64-bit host len can exceed 2^32 and the 64-bit end below
     * would wrap and under-refuse; on the 32-bit target len can
     * never reach this. */
    if ((uint64_t)len >= 0x100000000ull)
        return true;
    uint64_t start = addr;
    uint64_t end = start + (uint64_t)len;
    uint64_t region_start = g_ops->bootloader_addr;
    uint64_t region_end = region_start + (uint64_t)g_ops->bootloader_size;
    if (start < region_end && end > region_start)
        return true;
    /* The range wraps past 2^32: its tail re-enters at address 0, and
     * that tail can reach the region even though the linear interval
     * sits entirely above it. */
    if (end > 0xFFFFFFFFull && end - 0x100000000ull > region_start)
        return true;
    return false;
}

int eos_hal_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!g_ops || !g_ops->flash_write)
        return EOS_ERR_GENERIC;
    if (flash_range_hits_bootloader(addr, len))
        return EOS_ERR_WRITE_PROTECTED;
    return g_ops->flash_write(addr, buf, len);
}

int eos_hal_flash_erase(uint32_t addr, size_t len)
{
    if (!g_ops || !g_ops->flash_erase)
        return EOS_ERR_GENERIC;
    if (flash_range_hits_bootloader(addr, len))
        return EOS_ERR_WRITE_PROTECTED;
    return g_ops->flash_erase(addr, len);
}

void eos_hal_watchdog_init(uint32_t timeout_ms)
{
    if (g_ops && g_ops->watchdog_init)
        g_ops->watchdog_init(timeout_ms);
}

void eos_hal_watchdog_feed(void)
{
    if (g_ops && g_ops->watchdog_feed)
        g_ops->watchdog_feed();
}

eos_reset_reason_t eos_hal_get_reset_reason(void)
{
    if (g_ops && g_ops->get_reset_reason)
        return g_ops->get_reset_reason();
    return EOS_RESET_UNKNOWN;
}

void eos_hal_system_reset(void)
{
    if (g_ops && g_ops->system_reset)
        g_ops->system_reset();
    while (1); /* fallback halt */
}

bool eos_hal_recovery_pin_asserted(void)
{
    if (g_ops && g_ops->recovery_pin_asserted)
        return g_ops->recovery_pin_asserted();
    return false;
}

void eos_hal_jump(uint32_t vector_addr)
{
    if (g_ops && g_ops->jump)
        g_ops->jump(vector_addr);
}

int eos_hal_uart_init(uint32_t baud)
{
    if (!g_ops || !g_ops->uart_init)
        return EOS_ERR_GENERIC;
    return g_ops->uart_init(baud);
}

int eos_hal_uart_send(const void *buf, size_t len)
{
    if (!g_ops || !g_ops->uart_send)
        return EOS_ERR_GENERIC;
    return g_ops->uart_send(buf, len);
}

int eos_hal_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    if (!g_ops || !g_ops->uart_recv)
        return EOS_ERR_GENERIC;
    return g_ops->uart_recv(buf, len, timeout_ms);
}

uint32_t eos_hal_get_tick_ms(void)
{
    if (g_ops && g_ops->get_tick_ms)
        return g_ops->get_tick_ms();
    return 0;
}

void eos_hal_disable_interrupts(void)
{
    if (g_ops && g_ops->disable_interrupts)
        g_ops->disable_interrupts();
}

void eos_hal_enable_interrupts(void)
{
    if (g_ops && g_ops->enable_interrupts)
        g_ops->enable_interrupts();
}

void eos_hal_deinit_peripherals(void)
{
    if (g_ops && g_ops->deinit_peripherals)
        g_ops->deinit_peripherals();
}

uint32_t eos_hal_slot_addr(eos_slot_t slot)
{
    if (!g_ops)
        return 0;

    switch (slot) {
    case EOS_SLOT_A:        return g_ops->slot_a_addr;
    case EOS_SLOT_B:        return g_ops->slot_b_addr;
    case EOS_SLOT_RECOVERY: return g_ops->recovery_addr;
    default:                return 0;
    }
}

uint32_t eos_hal_slot_size(eos_slot_t slot)
{
    if (!g_ops)
        return 0;

    switch (slot) {
    case EOS_SLOT_A:        return g_ops->slot_a_size;
    case EOS_SLOT_B:        return g_ops->slot_b_size;
    case EOS_SLOT_RECOVERY: return g_ops->recovery_size;
    default:                return 0;
    }
}

/* --- Security Extension Wrappers --- */

int eos_hal_otp_read(uint32_t offset, void *buf, size_t len)
{
    if (!g_ops || !g_ops->otp_read)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->otp_read(offset, buf, len);
}

int eos_hal_otp_write(uint32_t offset, const void *buf, size_t len)
{
    if (!g_ops || !g_ops->otp_write)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->otp_write(offset, buf, len);
}

int eos_hal_rng_get(void *buf, size_t len)
{
    if (!g_ops || !g_ops->rng_get)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->rng_get(buf, len);
}

int eos_hal_debug_lock(void)
{
    if (!g_ops || !g_ops->debug_lock)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->debug_lock();
}

int eos_hal_debug_status(uint32_t *status)
{
    if (!g_ops || !g_ops->debug_status)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->debug_status(status);
}

int eos_hal_monotonic_read(uint32_t *value)
{
    if (!g_ops || !g_ops->monotonic_read)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->monotonic_read(value);
}

int eos_hal_monotonic_increment(void)
{
    if (!g_ops || !g_ops->monotonic_increment)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->monotonic_increment();
}

int eos_hal_flash_protect_bootloader(void)
{
    if (!g_ops || !g_ops->flash_protect_bootloader)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->flash_protect_bootloader();
}

int eos_hal_flash_protect_status(uint32_t *status)
{
    if (!status)
        return EOS_ERR_INVALID;
    if (!g_ops || !g_ops->flash_protect_status)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->flash_protect_status(status);
}

int eos_hal_flash_unprotect_bootloader(void)
{
    if (!g_ops || !g_ops->flash_unprotect_bootloader)
        return EOS_ERR_NOT_SUPPORTED;
    return g_ops->flash_unprotect_bootloader();
}

/* ---- Privileged bootloader-region access (ADR-022) ----
 *
 * The one-shot RAM authorization token. Stage-0's replacement
 * executor mints it only after the staged stage-1 image has fully
 * verified, and clears it when the authorized copy ends -- success
 * or failure -- so one mint authorizes exactly one copy, never a
 * later write. "Cleared on first use" in the design note is read
 * this way because a copy is many flash writes (erase + chunks): a
 * token that died on the first write could never complete one.
 * The token never persists across reset: it lives in .bss and RAM
 * does not survive one. C cannot enforce caller identity, so the
 * enforcement is structural -- the mint is declared in the
 * stage-0-local stage0/bootloader_replace.h, the executor is its
 * only caller, and the privileged entry points below are the only
 * way past the allow-list -- not a flag any caller can set. A
 * nonzero magic (not 0/1) so a zeroed .bss can never authorize. */
static volatile uint32_t g_bootloader_token = 0;
#define EOS_HAL_BTLDR_TOKEN_MAGIC  0xB7A9D1E5u

void eos_hal_bootloader_token_mint(void)
{
    g_bootloader_token = EOS_HAL_BTLDR_TOKEN_MAGIC;
}

void eos_hal_bootloader_token_clear(void)
{
    g_bootloader_token = 0;
}

/* The privileged range: the stage-1 part of the board-declared
 * bootloader region only, [stage1_base, bootloader_end). Stage-0 is
 * never writable through the privileged entries: it is the immutable
 * root of trust (ADR-022 section 1), and a privileged write there has
 * no fallback if power fails mid-copy. As merged in #200 the gate
 * admitted the whole bootloader region (stage-0 + stage-1); this
 * narrows it to what the design note defines "bootloader region" as.
 *
 * Where stage-1 starts is derived, not declared: stage1_base is
 * flash_base + app_vector_offset -- the same formula the stage-0
 * executor (stage0/bootloader_replace.c), the request-side verify
 * (core/bootloader_replace.c), and the stage-0 jump
 * (stage0/jump_stage1.c) already use for the stage-1 vector address.
 * There is no separate board-declared stage-1 field, so there is no
 * second source of truth to disagree with the jump target.
 *
 * Fail closed: a board whose derived stage-1 base does not lie
 * strictly inside its declared bootloader region declares no usable
 * stage-1 boundary, and the privileged entries refuse every range
 * rather than falling back to the whole region. That covers
 * app_vector_offset == 0 (stage-1 at the flash base: no stage-0 /
 * stage-1 split declared), an offset that lands at or below the
 * region start, one that lands at or past the region end, and an
 * offset that wraps flash_base past 2^32. */
/* 32-bit throughout: the range fits the stage-1 region only if the
 * length fits the region and the offset from the region start fits
 * what remains -- both subtractions are overflow-safe because the
 * preceding comparisons bound their operands. No 64-bit
 * arithmetic, which matters on a 32-bit MCU in a 16 KiB stage-0. */
static bool flash_range_inside_stage1(uint32_t addr, size_t len)
{
    uint32_t stage1_base;
    uint32_t stage1_size;
    if (!g_ops || g_ops->bootloader_size == 0)
        return false;
    if (g_ops->app_vector_offset > UINT32_MAX - g_ops->flash_base)
        return false;
    stage1_base = g_ops->flash_base + g_ops->app_vector_offset;
    if (stage1_base <= g_ops->bootloader_addr)
        return false;
    if (stage1_base - g_ops->bootloader_addr >= g_ops->bootloader_size)
        return false;
    stage1_size = g_ops->bootloader_size -
        (stage1_base - g_ops->bootloader_addr);
    if (len > stage1_size)
        return false;
    if (addr < stage1_base)
        return false;
    return addr - stage1_base <= stage1_size - (uint32_t)len;
}

/* The shared privileged-entry gate: the token, then the range,
 * then the board op. Write and erase differ only in which op
 * runs, so they share this one check instead of duplicating
 * it. The wrappers keep their own op-null check first,
 * preserving the original error order. */
static int privileged_bootloader_op(int is_write, uint32_t addr,
                                    const void *buf, size_t len)
{
    if (g_bootloader_token != EOS_HAL_BTLDR_TOKEN_MAGIC)
        return EOS_ERR_WRITE_PROTECTED;
    if (!flash_range_inside_stage1(addr, len))
        return EOS_ERR_INVALID;
    if (is_write)
        return g_ops->flash_write(addr, buf, len);
    return g_ops->flash_erase(addr, len);
}

int eos_hal_flash_write_bootloader(uint32_t addr, const void *buf, size_t len)
{
    if (!g_ops || !g_ops->flash_write)
        return EOS_ERR_GENERIC;
    return privileged_bootloader_op(1, addr, buf, len);
}

int eos_hal_flash_erase_bootloader(uint32_t addr, size_t len)
{
    if (!g_ops || !g_ops->flash_erase)
        return EOS_ERR_GENERIC;
    return privileged_bootloader_op(0, addr, NULL, len);
}
