// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_hal.h
 * @brief Hardware Abstraction Layer for eBootloader
 *
 * Board-agnostic interface for flash, watchdog, UART, reset,
 * and jump operations. Each target board provides its own
 * implementation of eos_board_ops_t.
 */

#ifndef EOS_HAL_H
#define EOS_HAL_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Board Operations ---------------- */

typedef struct {
    /* Memory map */
    uint32_t flash_base;
    uint32_t flash_size;
    uint32_t slot_a_addr;
    uint32_t slot_a_size;
    uint32_t slot_b_addr;
    uint32_t slot_b_size;
    uint32_t recovery_addr;
    uint32_t recovery_size;
    uint32_t bootctl_addr;
    uint32_t bootctl_backup_addr;
    uint32_t log_addr;
    uint32_t app_vector_offset;

    /* Bootloader-owned flash region [bootloader_addr,
     * bootloader_addr + bootloader_size): stage-0 + stage-1. The HAL
     * flash write/erase allow-list refuses any range intersecting it,
     * and flash_protect_bootloader() programs the hardware write
     * protection for it where the board supports one. A board that
     * declares no region (bootloader_size zero) gets no allow-list
     * enforcement:
     * protection is opt-in per board, and a board that cannot name its
     * bootloader region must not claim to protect it. */
    uint32_t bootloader_addr;
    uint32_t bootloader_size;

    /* Board identity for the bootloader-replacement target check
     * (ADR-022): the "<board_id>" half of an image's EOS_TLV_TARGET
     * value ("<board_id>:stage-1"). NULL means the board declares no
     * identity, and a replacement image can then never prove it is for
     * this board, so replacement is refused on that board. Two boards
     * can share an address map; this string is what distinguishes them.
     * STM32F4 declares "stm32f4". */
    const char *board_id;

    /* True when the board's bootloader protection changes (protect and
     * unprotect) take effect only at the next reset -- STM32F4 option
     * bytes are reloaded on reset (RM0090 §3.9). The replacement
     * executor reads this to decide whether lifting protection needs a
     * reset before the copy, or the copy can proceed in the same boot.
     * False (the default) means the change is immediate. */
    bool bootloader_protect_needs_reset;

    /* Flash operations */
    int (*flash_read)(uint32_t addr, void *buf, size_t len);
    int (*flash_write)(uint32_t addr, const void *buf, size_t len);
    int (*flash_erase)(uint32_t addr, size_t len);

    /* Watchdog */
    void (*watchdog_init)(uint32_t timeout_ms);
    void (*watchdog_feed)(void);

    /* Reset */
    eos_reset_reason_t (*get_reset_reason)(void);
    void (*system_reset)(void);

    /* Recovery pin */
    bool (*recovery_pin_asserted)(void);

    /* Jump / handoff */
    void (*jump)(uint32_t vector_addr);

    /* UART (for recovery transport) */
    int (*uart_init)(uint32_t baud);
    int (*uart_send)(const void *buf, size_t len);
    int (*uart_recv)(void *buf, size_t len, uint32_t timeout_ms);

    /* Timing */
    uint32_t (*get_tick_ms)(void);

    /* Interrupt control */
    void (*disable_interrupts)(void);
    void (*enable_interrupts)(void);
    void (*deinit_peripherals)(void);

    /* --- Security Extensions (Phase 2+) --- */
    /* All nullable — NULL means "not supported by this board". */

    /* OTP / eFuse secure storage */
    int (*otp_read)(uint32_t offset, void *buf, size_t len);
    int (*otp_write)(uint32_t offset, const void *buf, size_t len);

    /* Hardware random number generator */
    int (*rng_get)(void *buf, size_t len);

    /* Debug interface control */
    int (*debug_lock)(void);
    int (*debug_status)(uint32_t *status);

    /* Monotonic anti-rollback counter */
    int (*monotonic_read)(uint32_t *value);
    int (*monotonic_increment)(void);

    /* Bootloader self-protection: hardware write protection for the
     * board-declared bootloader region (see eos_bootloader_protect.h).
     * flash_protect_bootloader() programs the protection for the
     * region and verifies it by read-back; flash_protect_status()
     * reports whether the region is protected. NULL means "not
     * supported by this board". */
    int (*flash_protect_bootloader)(void);
    int (*flash_protect_status)(uint32_t *status);

    /* Bootloader self-update (ADR-022): lift the hardware write
     * protection for the board-declared bootloader region (e.g.
     * re-program WRP option bytes on STM32F4). The only caller is the
     * stage-0 replacement executor, after the staged image has fully
     * verified. NULL means "not supported by this board" and the
     * replacement is refused on that board. Whether the change takes
     * effect immediately or at the next reset is the board property
     * bootloader_protect_needs_reset above. */
    int (*flash_unprotect_bootloader)(void);

    /* HW-accelerated crypto (optional, software fallback used if NULL) */
    int (*hw_sha256)(const void *data, size_t len, void *digest);

    /* Reserved, and currently consumed by nothing.
     *
     * This signature cannot express a streaming AEAD: it carries no counter
     * position, so every chunk restarts the CTR keystream at block 0, and it
     * returns plaintext only, so there is no way to feed GHASH. core/
     * fw_decrypt.c used to call it and produced both keystream reuse and a
     * tag computed over an empty accumulator; the call site was removed
     * rather than patched, because feeding GHASH alone still leaves the
     * plaintext wrong past the first chunk.
     *
     * A board that implements this hook today gets nothing, silently. Do not
     * add a caller: define a streaming-capable contract first (init/update/
     * final, or an explicit block offset plus an AAD/tag path), which is a
     * change to this struct rather than to its consumers. */
    int (*hw_aes_decrypt)(const void *key, size_t key_len,
                          const void *iv, const void *in,
                          void *out, size_t len);
} eos_board_ops_t;

/* ---------------- Global Board Handle ---------------- */

/**
 * @brief Register board operations.
 * Must be called before any HAL function is used.
 * @param ops  Pointer to board operations (must remain valid).
 */
void eos_hal_init(const eos_board_ops_t *ops);

/**
 * @brief Get the registered board operations.
 * @return Pointer to the active board ops, or NULL if not initialized.
 */
const eos_board_ops_t *eos_hal_get_ops(void);

/* ---------------- HAL Convenience Functions ---------------- */

int   eos_hal_flash_read(uint32_t addr, void *buf, size_t len);
int   eos_hal_flash_write(uint32_t addr, const void *buf, size_t len);
int   eos_hal_flash_erase(uint32_t addr, size_t len);

void  eos_hal_watchdog_init(uint32_t timeout_ms);
void  eos_hal_watchdog_feed(void);

eos_reset_reason_t eos_hal_get_reset_reason(void);
void  eos_hal_system_reset(void);

bool  eos_hal_recovery_pin_asserted(void);

void  eos_hal_jump(uint32_t vector_addr);

int   eos_hal_uart_init(uint32_t baud);
int   eos_hal_uart_send(const void *buf, size_t len);
int   eos_hal_uart_recv(void *buf, size_t len, uint32_t timeout_ms);

uint32_t eos_hal_get_tick_ms(void);

void  eos_hal_disable_interrupts(void);
void  eos_hal_enable_interrupts(void);
void  eos_hal_deinit_peripherals(void);

/* Phase 2 security extensions */
int   eos_hal_otp_read(uint32_t offset, void *buf, size_t len);
int   eos_hal_otp_write(uint32_t offset, const void *buf, size_t len);
int   eos_hal_rng_get(void *buf, size_t len);
int   eos_hal_debug_lock(void);
int   eos_hal_debug_status(uint32_t *status);
int   eos_hal_monotonic_read(uint32_t *value);
int   eos_hal_monotonic_increment(void);
int   eos_hal_flash_protect_bootloader(void);
int   eos_hal_flash_protect_status(uint32_t *status);
int   eos_hal_flash_unprotect_bootloader(void);

/* Privileged bootloader-region flash access (ADR-022). These are the
 * single exemption from the HAL allow-list: they write/erase inside
 * the stage-1 part of the board-declared bootloader region
 * ([stage1_base, bootloader_end)), which eos_hal_flash_write()
 * and eos_hal_flash_erase() refuse unconditionally. Both calls are
 * gated on a one-shot RAM authorization token minted by stage-0's
 * replacement executor only after the staged image has fully
 * verified (see eos_hal_bootloader_token_mint(), declared in
 * stage0/bootloader_replace.h -- deliberately not here). Without a
 * minted token they refuse with EOS_ERR_WRITE_PROTECTED, exactly
 * like the allow-list, so an unprivileged caller gets the same
 * refusal from either path. A range outside the stage-1 region
 * is refused with EOS_ERR_INVALID: the privileged path writes the
 * stage-1 region and nothing else -- never stage-0. The stage-1
 * base is derived as flash_base + app_vector_offset (the stage-1
 * vector address); a board whose derived base does not lie strictly
 * inside its declared bootloader region has no usable stage-1
 * boundary and the privileged entries refuse every range. The
 * token never persists across reset (RAM does not survive one). */
int   eos_hal_flash_write_bootloader(uint32_t addr, const void *buf, size_t len);
int   eos_hal_flash_erase_bootloader(uint32_t addr, size_t len);

/**
 * @brief Get the flash address for a given slot.
 * @param slot  Slot identifier.
 * @return Flash address of the slot, or 0 if invalid.
 */
uint32_t eos_hal_slot_addr(eos_slot_t slot);

/**
 * @brief Get the size of a given slot.
 * @param slot  Slot identifier.
 * @return Size in bytes, or 0 if invalid.
 */
uint32_t eos_hal_slot_size(eos_slot_t slot);

/* ---------------- Convenience Aliases ---------------- */

#define eos_hal_uart_write(port, data, len) eos_hal_uart_send(data, len)
#define eos_hal_uart_read(port, data, len, timeout) eos_hal_uart_recv(data, len, timeout)
#define eos_hal_disable_irqs()  eos_hal_disable_interrupts()
#define eos_hal_enable_irqs()   eos_hal_enable_interrupts()

static inline void eos_hal_set_msp(uint32_t addr) {
#if defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M')
    __asm volatile ("MSR MSP, %0" : : "r" (addr));
#elif defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'R')
    __asm volatile ("MOV SP, %0" : : "r" (addr));
#elif defined(__aarch64__)
    __asm volatile ("MOV SP, %0" : : "r" ((uint64_t)addr));
#elif defined(__ARM_ARCH) && (__ARM_ARCH <= 7)
    __asm volatile ("MOV SP, %0" : : "r" (addr));
#elif defined(__riscv)
    __asm volatile ("mv sp, %0" : : "r" (addr));
#else
    (void)addr;
#endif
}

typedef enum {
    EOS_PLATFORM_ARM_CM0   = 0,
    EOS_PLATFORM_ARM_CM3   = 1,
    EOS_PLATFORM_ARM_CM4   = 2,
    EOS_PLATFORM_ARM_CM7   = 3,
    EOS_PLATFORM_ARM_CM33  = 4,
    EOS_PLATFORM_ARM_CA53  = 5,
    EOS_PLATFORM_ARM_CA72  = 6,
    EOS_PLATFORM_ARM_R5F   = 7,
    EOS_PLATFORM_RISCV32   = 8,
    EOS_PLATFORM_RISCV64   = 9,
    EOS_PLATFORM_X86       = 10,
    EOS_PLATFORM_X86_64    = 11,
    EOS_PLATFORM_XTENSA    = 12,
    EOS_PLATFORM_POWERPC   = 13,
    EOS_PLATFORM_SPARC     = 14,
    EOS_PLATFORM_M68K      = 15,
    EOS_PLATFORM_SH4       = 16,
    EOS_PLATFORM_MN103     = 17,
    EOS_PLATFORM_V850      = 18,
    EOS_PLATFORM_FRV       = 19,
    EOS_PLATFORM_H8300     = 20,
    EOS_PLATFORM_STRONGARM = 21,
    EOS_PLATFORM_XSCALE    = 22,
    EOS_PLATFORM_MIPS      = 23,
} eos_platform_t;

#ifdef __cplusplus
}
#endif
#endif /* EOS_HAL_H */
