// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_stm32f4.c
 * @brief STM32F4 reference board port
 *
 * Reference HAL implementation for STM32F407. Uses direct register
 * access to avoid CMSIS/HAL library dependencies for portability.
 */

#include "board_stm32f4.h"
#include "eos_bootloader_protect.h"
#include "eos_debug_lock.h"
#include <string.h>

/* ---- STM32F4 Register Definitions ---- */

/* RCC */
#define RCC_BASE        0x40023800
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

/* Flash interface */
#ifdef EBLDR_TEST_FLASH_REGS
/* Host unit tests redirect the flash registers at a RAM array so the OTP
 * burn path runs without hardware: tests/unit/test_stm32f4_otp_counter.c
 * compiles this file with EBLDR_TEST_FLASH_REGS and provides
 * eboot_test_flash_regs[]. Device builds are unaffected. */
extern uint32_t eboot_test_flash_regs[];
#define FLASH_IF_BASE   ((uintptr_t)eboot_test_flash_regs)
#else
#define FLASH_IF_BASE   0x40023C00
#endif
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_IF_BASE + 0x00))
#define FLASH_KEYR      (*(volatile uint32_t *)(FLASH_IF_BASE + 0x04))
#define FLASH_OPTKEYR   (*(volatile uint32_t *)(FLASH_IF_BASE + 0x08))
#define FLASH_SR        (*(volatile uint32_t *)(FLASH_IF_BASE + 0x0C))
#define FLASH_CR        (*(volatile uint32_t *)(FLASH_IF_BASE + 0x10))
#define FLASH_OPTCR     (*(volatile uint32_t *)(FLASH_IF_BASE + 0x14))

#define FLASH_KEY1      0x45670123
#define FLASH_KEY2      0xCDEF89AB

/* Option-byte unlock keys (RM0090 §3.9.2). */
#define FLASH_OPTKEY1   0x08192A3B
#define FLASH_OPTKEY2   0x4C5D6E7F

/* FLASH_OPTCR layout (RM0090 §3.9.7): OPTLOCK bit 0, OPTSTRT bit 1,
 * RDP[7:0] bits 15:8. */
#define FLASH_OPTCR_OPTLOCK     (1 << 0)
#define FLASH_OPTCR_OPTSTRT     (1 << 1)
#define FLASH_OPTCR_RDP_SHIFT   8
#define FLASH_OPTCR_RDP_MASK    (0xFF << FLASH_OPTCR_RDP_SHIFT)

/* RDP levels (RM0090 §3.9.1): 0xAA is level 0 (no protection); 0xCC is
 * level 2, which is IRREVERSIBLE -- no debug access ever again, and the
 * level cannot be lowered -- so nothing in this file ever writes it;
 * any other value is level 1 (debug disabled, recoverable by mass
 * erase). The lock programs level 1 as 0x00. */
#define STM32F4_RDP_LEVEL_0     0xAA
#define STM32F4_RDP_LEVEL_1     0x00
#define STM32F4_RDP_LEVEL_2     0xCC

/* WRP (write protection) field (RM0090 §3.9.7): FLASH_OPTCR bits 27:16
 * are nWRP[11:0], one per flash sector 0-11; a CLEARED bit write-protects
 * its sector, a set bit leaves it writable. The erased value is all set
 * (no sector protected). Protection takes effect at the next reset: the
 * option bytes are reloaded on reset, like the RDP level. WRP is
 * reversible -- clearing the bits again (or a mass erase) removes it --
 * unlike RDP level 2, so no irreversible state is created here. */
#define FLASH_OPTCR_NWRP_SHIFT  16

/* IWDG */
#define IWDG_BASE       0x40003000
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0C))

/* USART2 */
#define USART2_BASE     0x40004400
#define USART2_SR       (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR       (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR      (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1      (*(volatile uint32_t *)(USART2_BASE + 0x0C))

/* GPIO */
#define GPIOA_BASE      0x40020000
#define GPIOC_BASE      0x40020800
#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_IDR(b)     (*(volatile uint32_t *)((b) + 0x10))
#define GPIO_AFRL(b)    (*(volatile uint32_t *)((b) + 0x20))

/* SCB */
#define SCB_VTOR        (*(volatile uint32_t *)0xE000ED08)
#define SCB_AIRCR       (*(volatile uint32_t *)0xE000ED0C)

/* SysTick */
#define SYSTICK_CTRL    (*(volatile uint32_t *)0xE000E010)
#define SYSTICK_LOAD    (*(volatile uint32_t *)0xE000E014)
#define SYSTICK_VAL     (*(volatile uint32_t *)0xE000E018)

/* NVIC */
#define NVIC_ICER_BASE  0xE000E180

static volatile uint32_t tick_ms = 0;

/* ---- Flash Operations ---- */

static int stm32f4_flash_read(uint32_t addr, void *buf, size_t len)
{
    memcpy(buf, (const void *)(uintptr_t)addr, len);
    return EOS_OK;
}

static void flash_unlock(void)
{
    if (FLASH_CR & (1 << 31)) { /* LOCK bit */
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH_CR |= (1 << 31);
}

static void flash_wait_busy(void)
{
    while (FLASH_SR & (1 << 16)) /* BSY */
        ;
}

/* Program bytes through the flash interface (PSIZE x8, byte by byte).
 * Shared by flash_write and the OTP write below: OTP bytes on the F407
 * program exactly like flash bytes (RM0090 §3.7). Takes a uintptr_t so a
 * host unit test can pass the redirected OTP window (a RAM address, which
 * does not fit in the HAL's uint32_t address type). */
static int flash_program_bytes(uintptr_t addr, const uint8_t *buf, size_t len)
{
    flash_unlock();
    flash_wait_busy();

    /* Program byte-by-byte (PSIZE = 00 for x8) */
    FLASH_CR &= ~(3 << 8); /* Clear PSIZE */
    FLASH_CR |= (1 << 0);  /* PG bit */

    for (size_t i = 0; i < len; i++) {
        *(volatile uint8_t *)(addr + i) = buf[i];
        flash_wait_busy();

        if (FLASH_SR & 0xF0) { /* Error flags */
            FLASH_SR = 0xF0; /* Clear errors */
            FLASH_CR &= ~(1 << 0);
            flash_lock();
            return EOS_ERR_FLASH;
        }
    }

    FLASH_CR &= ~(1 << 0); /* Clear PG */
    flash_lock();
    return EOS_OK;
}

static int stm32f4_flash_write(uint32_t addr, const void *buf, size_t len)
{
    return flash_program_bytes(addr, (const uint8_t *)buf, len);
}

static int stm32f4_flash_erase(uint32_t addr, size_t len)
{
    /* Erase every sector [addr, addr + len) touches. Only the sector
     * holding addr used to be erased and len was ignored, so erasing a
     * 256K slot cleared its first sector and left stale image bytes in
     * the rest -- which the following program step cannot overwrite. */
    int first, last;
    if (stm32f4_flash_sector_range(addr, len, &first, &last) != 0)
        return EOS_ERR_INVALID;

    flash_unlock();

    for (int sector = first; sector <= last; sector++) {
        flash_wait_busy();

        FLASH_CR &= ~(0xF << 3);      /* Clear SNB bits */
        FLASH_CR |= (sector << 3);    /* Set sector number */
        FLASH_CR |= (1 << 1);         /* SER — sector erase */
        FLASH_CR |= (1 << 16);        /* STRT — start erase */

        flash_wait_busy();

        if (FLASH_SR & 0xF0) {
            FLASH_SR = 0xF0;
            FLASH_CR &= ~(1 << 1);
            flash_lock();
            return EOS_ERR_FLASH;
        }
    }

    FLASH_CR &= ~(1 << 1); /* Clear SER */
    flash_lock();
    return EOS_OK;
}

/* ---- OTP (one-time programmable) and monotonic counter ---- */

/* The OTP array the counter lives in. On device it is the STM32F407 OTP
 * array (RM0090 §3.5); host unit tests redirect it at a RAM buffer so the
 * counter logic runs without hardware (tests/unit/test_stm32f4_otp_counter.c
 * compiles this file with EBLDR_TEST_OTP_WINDOW and provides the array). */
#ifdef EBLDR_TEST_OTP_WINDOW
extern uint8_t eboot_test_otp_window[];
#define STM32F4_OTP_WINDOW ((uintptr_t)eboot_test_otp_window)
#else
#define STM32F4_OTP_WINDOW ((uintptr_t)STM32F4_OTP_BASE)
#endif

static int stm32f4_otp_read(uint32_t offset, void *buf, size_t len)
{
    if (!buf) return EOS_ERR_INVALID;
    if ((uint64_t)offset + len > STM32F4_OTP_SIZE) return EOS_ERR_INVALID;
    memcpy(buf, (const void *)(STM32F4_OTP_WINDOW + offset), len);
    return EOS_OK;
}

static int stm32f4_otp_write(uint32_t offset, const void *buf, size_t len)
{
    if (!buf) return EOS_ERR_INVALID;
    if ((uint64_t)offset + len > STM32F4_OTP_SIZE) return EOS_ERR_INVALID;

    const uint8_t *src = (const uint8_t *)buf;

    /* OTP bits only clear (1 -> 0). A write that asks to set a bit cannot
     * be performed; refuse the whole write before programming anything, so
     * a failed write leaves the field untouched instead of half-written.
     * Callers that need to set bits are asking for RAM semantics from
     * one-time storage, and silently programming only the clearable part
     * would report success for bytes that did not land. */
    for (size_t i = 0; i < len; i++) {
        uint8_t current = *(volatile uint8_t *)(STM32F4_OTP_WINDOW + offset + i);
        if ((uint8_t)(src[i] & (uint8_t)~current) != 0)
            return EOS_ERR_INVALID;
    }

    if (len == 0) return EOS_OK;

    int rc = flash_program_bytes(STM32F4_OTP_WINDOW + offset, src, len);
    if (rc != EOS_OK) return rc;

    /* A fuse burn can fail without reporting an error: verify the bytes
     * read back are the bytes asked for. */
    for (size_t i = 0; i < len; i++) {
        uint8_t back = *(volatile uint8_t *)(STM32F4_OTP_WINDOW + offset + i);
        if (back != src[i]) return EOS_ERR_FLASH;
    }
    return EOS_OK;
}

/* ---- Monotonic anti-rollback counter ----
 *
 * The device floor is the number of burned bits in the security_version
 * OTP field (STM32F4_OTP_COUNTER_OFFSET, 4 bytes). Read counts the bits;
 * increment burns the lowest set bit and reads the count back to prove it
 * moved. Nothing here writes outside the field: the single byte
 * monotonic_increment programs is inside it, and otp_write refuses any
 * range that leaves the 512-byte OTP array -- so the recovery secret at
 * 0x180 is never touched by this counter (#159).
 *
 * Host and sim builds keep NOT_SUPPORTED: they have no otp_read, so
 * eos_rollback_verify() fails closed there, and the counter exists only
 * on this board until another port implements it. */

static int stm32f4_monotonic_read(uint32_t *value)
{
    if (!value) return EOS_ERR_INVALID;
    uint8_t field[STM32F4_OTP_COUNTER_SIZE];
    int rc = stm32f4_otp_read(STM32F4_OTP_COUNTER_OFFSET, field, sizeof(field));
    if (rc != EOS_OK) return rc;
    *value = stm32f4_otp_counter_count(field, sizeof(field));
    return EOS_OK;
}

static int stm32f4_monotonic_increment(void)
{
    uint8_t field[STM32F4_OTP_COUNTER_SIZE];
    int rc = stm32f4_otp_read(STM32F4_OTP_COUNTER_OFFSET, field, sizeof(field));
    if (rc != EOS_OK) return rc;

    uint32_t byte = 0;
    uint8_t burned = 0;
    if (stm32f4_otp_counter_next_burn(field, sizeof(field), &byte, &burned) != 0)
        return EOS_ERR_FULL; /* All 32 fuses burned: the floor cannot rise. */

    uint32_t before = stm32f4_otp_counter_count(field, sizeof(field));

    rc = stm32f4_otp_write(STM32F4_OTP_COUNTER_OFFSET + byte, &burned, 1);
    if (rc != EOS_OK) return rc;

    /* A fuse burn can fail without reporting an error: the count must have
     * advanced by exactly one. */
    rc = stm32f4_otp_read(STM32F4_OTP_COUNTER_OFFSET, field, sizeof(field));
    if (rc != EOS_OK) return rc;
    if (stm32f4_otp_counter_count(field, sizeof(field)) != before + 1)
        return EOS_ERR_FLASH;
    return EOS_OK;
}

/* ---- Debug lock (RDP option bytes) ----
 *
 * STM32F4 debug protection is the RDP level in the FLASH_OPTCR option
 * bytes (RM0090 §3.9), not a user OTP byte: secure_boot used to write
 * 0xFF to OTP 0x1F0, which is a no-op on erased OTP and locks nothing
 * (#184). Programming the option bytes erases and rewrites the whole
 * option-byte page, so the lock is idempotent -- an already-locked
 * device returns EOS_OK without programming anything -- and it
 * preserves every other option bit, touching only the RDP field.
 *
 * The programmed level takes effect at the next system reset (RM0090
 * §3.9: option bytes are reloaded on reset); the lock is verified here
 * by reading the programmed RDP field back out of FLASH_OPTCR, which
 * proves the lock is programmed and durable.
 *
 * A failed option unlock cannot be told apart from a successful one by
 * reading OPTLOCK back in the host unit-test model, where the registers
 * are RAM and the unlock has no side effect -- so no post-unlock check
 * is made here. The post-program read-back is the check that catches
 * it: if the unlock failed, the OPTCR write is ignored and the RDP
 * field still reads level 0. */

static uint32_t stm32f4_rdp_level(void)
{
    return (FLASH_OPTCR >> FLASH_OPTCR_RDP_SHIFT) & 0xFF;
}

static int stm32f4_debug_status(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;

    uint32_t rdp = stm32f4_rdp_level();
    if (rdp == STM32F4_RDP_LEVEL_0)
        *status = EOS_DEBUG_STATUS_UNLOCKED;
    else if (rdp == STM32F4_RDP_LEVEL_2)
        *status = EOS_DEBUG_STATUS_PERMANENT;
    else
        *status = EOS_DEBUG_STATUS_LOCKED;
    return EOS_OK;
}

static int stm32f4_debug_lock(void)
{
    /* Already locked (level 1) or permanent (level 2): nothing to
     * program. Level 2 is never programmed here -- it is irreversible --
     * so a device found at level 2 simply reports success. */
    uint32_t rdp = stm32f4_rdp_level();
    if (rdp != STM32F4_RDP_LEVEL_0)
        return EOS_OK;

    /* Unlock the option bytes. */
    if (FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        FLASH_OPTKEYR = FLASH_OPTKEY2;
    }

    /* Program RDP level 1, preserving every other option bit. The
     * launch is a separate step: write the value with OPTSTRT clear,
     * then set OPTSTRT to start the option-byte program. */
    uint32_t optcr = FLASH_OPTCR;
    optcr &= ~FLASH_OPTCR_RDP_MASK;
    optcr |= (uint32_t)STM32F4_RDP_LEVEL_1 << FLASH_OPTCR_RDP_SHIFT;
    optcr &= ~(uint32_t)FLASH_OPTCR_OPTSTRT;
    FLASH_OPTCR = optcr;

    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    flash_wait_busy();

    if (FLASH_SR & 0xF0) {
        FLASH_SR = 0xF0; /* Clear errors */
        FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;
        return EOS_ERR_FLASH;
    }
    FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;

    /* A program can fail without reporting an error: the RDP field
     * must read back as the level programmed. */
    if (stm32f4_rdp_level() != STM32F4_RDP_LEVEL_1)
        return EOS_ERR_FLASH;
    return EOS_OK;
}

/* ---- Bootloader self-protection (WRP option bytes) ----
 *
 * The bootloader region (stage-0 + stage-1, sectors 0-3) is
 * write-protected by clearing its nWRP bits in FLASH_OPTCR, using the
 * same option-byte program sequence as the RDP debug lock above:
 * unlock the option bytes, write OPTCR with OPTSTRT clear, set OPTSTRT
 * to launch, wait, check the error flags, re-lock -- and then read the
 * nWRP field back, because a program can fail without reporting an
 * error and the read-back is the check that catches it. Every other
 * option bit is preserved: only the bootloader sectors' nWRP bits are
 * touched, so programming WRP never changes the RDP level (and
 * programming RDP never changes WRP -- the debug-lock test pins the
 * other direction).
 *
 * The call is idempotent: a device whose bootloader sectors are
 * already protected returns EOS_OK without programming anything.
 * WRP takes effect at the next reset (RM0090 §3.9: option bytes are
 * reloaded on reset); the HAL allow-list covers the same region from
 * the first boot, so the window between programming and reset is
 * guarded in software.
 *
 * A failed option unlock cannot be told apart from a successful one by
 * reading OPTLOCK back in the host unit-test model, where the registers
 * are RAM and the unlock has no side effect -- so no post-unlock check
 * is made here. The post-program read-back is the check that catches
 * it: if the unlock failed, the OPTCR write is ignored and the nWRP
 * field still reads all set. */

static uint32_t stm32f4_bootloader_nwrp_mask(void)
{
    int first, last;
    if (stm32f4_flash_sector_range(STM32F4_BOOTLOADER_ADDR,
                                   STM32F4_BOOTLOADER_SIZE,
                                   &first, &last) != 0)
        return 0;
    /* Sectors 0-3 for the bootloader region; the mask must never cover
     * a sector outside it, so it is built from the sector range, not
     * from a hardcoded constant. */
    return (((1u << (uint32_t)(last - first + 1)) - 1u) << (uint32_t)first)
           << FLASH_OPTCR_NWRP_SHIFT;
}

static int stm32f4_flash_protect_status(uint32_t *status)
{
    if (!status) return EOS_ERR_INVALID;

    uint32_t mask = stm32f4_bootloader_nwrp_mask();
    if (mask == 0) return EOS_ERR_INVALID;

    /* Protected only when every bootloader sector's nWRP bit is clear.
     * A partially protected region is unprotected: the bootloader is
     * one writable sector away from being overwritten. */
    *status = ((FLASH_OPTCR & mask) == 0)
        ? EOS_BOOTLOADER_PROTECT_PROTECTED
        : EOS_BOOTLOADER_PROTECT_UNPROTECTED;
    return EOS_OK;
}

static int stm32f4_flash_protect_bootloader(void)
{
    uint32_t mask = stm32f4_bootloader_nwrp_mask();
    if (mask == 0) return EOS_ERR_INVALID;

    /* Already protected: nothing to program. */
    if ((FLASH_OPTCR & mask) == 0)
        return EOS_OK;

    /* Unlock the option bytes. */
    if (FLASH_OPTCR & FLASH_OPTCR_OPTLOCK) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        FLASH_OPTKEYR = FLASH_OPTKEY2;
    }

    /* Clear the bootloader sectors' nWRP bits, preserving every other
     * option bit. The launch is a separate step: write the value with
     * OPTSTRT clear, then set OPTSTRT to start the option-byte program. */
    uint32_t optcr = FLASH_OPTCR;
    optcr &= ~mask;
    optcr &= ~(uint32_t)FLASH_OPTCR_OPTSTRT;
    FLASH_OPTCR = optcr;

    FLASH_OPTCR |= FLASH_OPTCR_OPTSTRT;
    flash_wait_busy();

    if (FLASH_SR & 0xF0) {
        FLASH_SR = 0xF0; /* Clear errors */
        FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;
        return EOS_ERR_FLASH;
    }
    FLASH_OPTCR |= FLASH_OPTCR_OPTLOCK;

    /* A program can fail without reporting an error: every bootloader
     * sector's nWRP bit must read back clear. */
    if ((FLASH_OPTCR & mask) != 0)
        return EOS_ERR_FLASH;
    return EOS_OK;
}

/* ---- Watchdog ---- */

static void stm32f4_watchdog_init(uint32_t timeout_ms)
{
    /* LSI ≈ 32kHz. Prescaler /256 → ~125 Hz */
    IWDG_KR = 0x5555; /* Enable write access */
    IWDG_PR = 6;      /* Prescaler /256 */
    IWDG_RLR = (timeout_ms * 125) / 1000;
    if (IWDG_RLR > 0xFFF) IWDG_RLR = 0xFFF;
    IWDG_KR = 0xCCCC; /* Start IWDG */
}

static void stm32f4_watchdog_feed(void)
{
    IWDG_KR = 0xAAAA; /* Reload */
}

/* ---- Reset ---- */

static eos_reset_reason_t stm32f4_get_reset_reason(void)
{
    #define RCC_CSR (*(volatile uint32_t *)(RCC_BASE + 0x74))
    uint32_t csr = RCC_CSR;
    RCC_CSR |= (1 << 24); /* RMVF — clear flags */

    if (csr & (1 << 29)) return EOS_RESET_WATCHDOG;
    if (csr & (1 << 28)) return EOS_RESET_SOFTWARE;
    if (csr & (1 << 26)) return EOS_RESET_PIN;
    if (csr & (1 << 25)) return EOS_RESET_BROWNOUT;
    if (csr & (1 << 31)) return EOS_RESET_LOCKUP;

    return EOS_RESET_POWER_ON;
}

static void stm32f4_system_reset(void)
{
    SCB_AIRCR = (0x5FA << 16) | (1 << 2); /* SYSRESETREQ */
    while (1);
}

/* ---- Recovery Pin ---- */

static bool stm32f4_recovery_pin_asserted(void)
{
    /* PC13 — active low (pressed = 0) */
    RCC_AHB1ENR |= (1 << 2); /* Enable GPIOC clock */
    GPIO_MODER(GPIOC_BASE) &= ~(3 << (STM32F4_RECOVERY_PIN * 2)); /* Input mode */
    return !(GPIO_IDR(GPIOC_BASE) & (1 << STM32F4_RECOVERY_PIN));
}

/* ---- Jump ---- */

static void stm32f4_jump(uint32_t vector_addr)
{
#if defined(__ARM_ARCH) || defined(__arm__)
    uint32_t new_msp  = *(volatile uint32_t *)(uintptr_t)(vector_addr + 0);
    uint32_t reset_pc = *(volatile uint32_t *)(uintptr_t)(vector_addr + 4);

    SCB_VTOR = vector_addr;

    __asm volatile ("MSR MSP, %0" : : "r" (new_msp));
    __asm volatile ("BX %0" : : "r" (reset_pc));
#else
    (void)vector_addr;
#endif
}

/* ---- UART ---- */

static int stm32f4_uart_init(uint32_t baud)
{
    RCC_AHB1ENR |= (1 << 0);  /* Enable GPIOA clock */
    RCC_APB1ENR |= (1 << 17); /* Enable USART2 clock */

    /* PA2 = TX (AF7), PA3 = RX (AF7) */
    GPIO_MODER(GPIOA_BASE) &= ~((3 << 4) | (3 << 6));
    GPIO_MODER(GPIOA_BASE) |=  ((2 << 4) | (2 << 6)); /* Alternate function */
    GPIO_AFRL(GPIOA_BASE)  &= ~((0xF << 8) | (0xF << 12));
    GPIO_AFRL(GPIOA_BASE)  |=  ((7 << 8) | (7 << 12));   /* AF7 */

    /* Baud rate: assuming 16 MHz APB1 clock */
    USART2_BRR = 16000000 / baud;
    USART2_CR1 = (1 << 13) | (1 << 3) | (1 << 2); /* UE, TE, RE */

    return EOS_OK;
}

static int stm32f4_uart_send(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        while (!(USART2_SR & (1 << 7))); /* TXE */
        USART2_DR = p[i];
    }
    while (!(USART2_SR & (1 << 6))); /* TC */
    return EOS_OK;
}

static int stm32f4_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    uint8_t *p = (uint8_t *)buf;
    uint32_t start = tick_ms;

    for (size_t i = 0; i < len; i++) {
        while (!(USART2_SR & (1 << 5))) { /* RXNE */
            if ((tick_ms - start) >= timeout_ms)
                return EOS_ERR_TIMEOUT;
        }
        p[i] = (uint8_t)USART2_DR;
    }
    return EOS_OK;
}

/* ---- Timing ---- */

static uint32_t stm32f4_get_tick_ms(void)
{
    return tick_ms;
}

/* SysTick handler — increments every 1ms */
void SysTick_Handler(void)
{
    tick_ms++;
}

/* ---- Interrupt Control ---- */

static void stm32f4_disable_interrupts(void)
{
#if defined(__ARM_ARCH) || defined(__arm__)
    __asm volatile ("CPSID i");
#endif
}

static void stm32f4_enable_interrupts(void)
{
#if defined(__ARM_ARCH) || defined(__arm__)
    __asm volatile ("CPSIE i");
#endif
}

static void stm32f4_deinit_peripherals(void)
{
    /* Disable SysTick */
    SYSTICK_CTRL = 0;

    /* Disable all NVIC interrupts */
    for (int i = 0; i < 8; i++) {
        *(volatile uint32_t *)(uintptr_t)(NVIC_ICER_BASE + i * 4) = 0xFFFFFFFF;
    }

    /* Disable USART2 */
    USART2_CR1 = 0;
}

/* ---- Board Ops ---- */

static const eos_board_ops_t stm32f4_ops = {
    .flash_base          = STM32F4_FLASH_BASE,
    .flash_size          = STM32F4_FLASH_SIZE,
    .slot_a_addr         = STM32F4_SLOT_A_ADDR,
    .slot_a_size         = STM32F4_SLOT_A_SIZE,
    .slot_b_addr         = STM32F4_SLOT_B_ADDR,
    .slot_b_size         = STM32F4_SLOT_B_SIZE,
    .recovery_addr       = STM32F4_RECOVERY_ADDR,
    .recovery_size       = STM32F4_RECOVERY_SIZE,
    .bootctl_addr        = STM32F4_BOOTCTL_ADDR,
    .bootctl_backup_addr = STM32F4_BOOTCTL_BACKUP_ADDR,
    .log_addr            = STM32F4_LOG_ADDR,
    .app_vector_offset   = STM32F4_VECTOR_OFFSET,
    .bootloader_addr     = STM32F4_BOOTLOADER_ADDR,
    .bootloader_size     = STM32F4_BOOTLOADER_SIZE,

    .flash_read          = stm32f4_flash_read,
    .flash_write         = stm32f4_flash_write,
    .flash_erase         = stm32f4_flash_erase,

    .watchdog_init       = stm32f4_watchdog_init,
    .watchdog_feed       = stm32f4_watchdog_feed,

    .get_reset_reason    = stm32f4_get_reset_reason,
    .system_reset        = stm32f4_system_reset,

    .recovery_pin_asserted = stm32f4_recovery_pin_asserted,

    .jump                = stm32f4_jump,

    .uart_init           = stm32f4_uart_init,
    .uart_send           = stm32f4_uart_send,
    .uart_recv           = stm32f4_uart_recv,

    .get_tick_ms         = stm32f4_get_tick_ms,

    .disable_interrupts  = stm32f4_disable_interrupts,
    .enable_interrupts   = stm32f4_enable_interrupts,
    .deinit_peripherals  = stm32f4_deinit_peripherals,

    .otp_read            = stm32f4_otp_read,
    .otp_write           = stm32f4_otp_write,

    .debug_lock          = stm32f4_debug_lock,
    .debug_status        = stm32f4_debug_status,

    .monotonic_read      = stm32f4_monotonic_read,
    .monotonic_increment = stm32f4_monotonic_increment,

    .flash_protect_bootloader = stm32f4_flash_protect_bootloader,
    .flash_protect_status     = stm32f4_flash_protect_status,
};

/* ---- Board Entry Points ---- */

void board_early_init(void)
{
    /* Enable HSI (16 MHz internal) — already running at reset */
    /* Set flash latency for 16 MHz */
    FLASH_ACR = (FLASH_ACR & ~0xF) | 0;

    /* Enable GPIOA, GPIOC clocks */
    RCC_AHB1ENR |= (1 << 0) | (1 << 2);

    /* Configure SysTick for 1ms interrupts (16 MHz / 1000) */
    SYSTICK_LOAD = 16000 - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = (1 << 0) | (1 << 1) | (1 << 2); /* Enable, interrupt, use processor clock */
}

const eos_board_ops_t *board_get_ops(void)
{
    return &stm32f4_ops;
}
