// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file board_x86.c
 * @brief x86 board port (i486/Pentium, legacy PC)
 *
 * Full board implementation for x86 legacy PC platform.
 * Uses port I/O for flash (LPC/SPI mapped), COM1 UART,
 * PIT for timing, and standard PC reset mechanisms.
 */

#include "eos_hal.h"
#include "eos_types.h"
#include "eos_device_table.h"

#define X86_FLASH_BASE    0xFFF00000
#define X86_FLASH_SIZE    (16 * 1024 * 1024)
#define X86_RAM_BASE      0x00100000
#define X86_RAM_SIZE      (64 * 1024 * 1024)
#define X86_SLOT_A        0xFF040000
#define X86_SLOT_B        0xFF640000
#define X86_RECOVERY      0xFFC40000
#define X86_BOOTCTL       0xFFEFF000

/* x86 port I/O helpers */
static inline void outb(uint16_t port, uint8_t val)
{
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
#else
    (void)port; (void)val;
#endif
}

static inline uint8_t inb(uint16_t port)
{
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
#else
    (void)port;
    return 0xFF;
#endif
}

/* ---- Flash operations (memory-mapped NOR flash) ---- */

static int x86_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!buf || addr < X86_FLASH_BASE ||
        (addr + len) > (X86_FLASH_BASE + X86_FLASH_SIZE)) {
        return EOS_ERR_INVALID;
    }
    const volatile uint8_t *src = (const volatile uint8_t *)(uintptr_t)addr;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    return EOS_OK;
}

static int x86_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!buf || addr < X86_FLASH_BASE ||
        (addr + len) > (X86_FLASH_BASE + X86_FLASH_SIZE)) {
        return EOS_ERR_INVALID;
    }
    volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)addr;
    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        /* NOR flash byte-program sequence (CFI-compatible) */
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x5555) = 0xAA;
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x2AAA) = 0x55;
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x5555) = 0xA0;
        dst[i] = src[i];
        /* Poll for completion (data# polling) */
        while ((dst[i] & 0x80) != (src[i] & 0x80)) {}
    }
    return EOS_OK;
}

static int x86_flash_erase(uint32_t addr, size_t len)
{
    if (addr < X86_FLASH_BASE ||
        (addr + len) > (X86_FLASH_BASE + X86_FLASH_SIZE)) {
        return EOS_ERR_INVALID;
    }
    /* Sector erase: NOR flash CFI sector-erase command sequence */
    uint32_t sector_size = 4096;
    uint32_t aligned_addr = addr & ~(sector_size - 1);
    uint32_t end = addr + len;
    while (aligned_addr < end) {
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x5555) = 0xAA;
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x2AAA) = 0x55;
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x5555) = 0x80;
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x5555) = 0xAA;
        *(volatile uint8_t *)(uintptr_t)(X86_FLASH_BASE + 0x2AAA) = 0x55;
        *(volatile uint8_t *)(uintptr_t)aligned_addr = 0x30;
        /* Poll for erase completion */
        while (!(*(volatile uint8_t *)(uintptr_t)aligned_addr & 0x80)) {}
        aligned_addr += sector_size;
    }
    return EOS_OK;
}

/* ---- COM1 UART (8250/16550 at 0x3F8) ---- */

#define COM1_BASE  0x3F8
#define COM1_DATA  (COM1_BASE + 0)
#define COM1_IER   (COM1_BASE + 1)
#define COM1_FCR   (COM1_BASE + 2)
#define COM1_LCR   (COM1_BASE + 3)
#define COM1_MCR   (COM1_BASE + 4)
#define COM1_LSR   (COM1_BASE + 5)

static int x86_uart_init(uint32_t baud)
{
    uint16_t divisor = (uint16_t)(115200 / baud);
    outb(COM1_IER, 0x00);           /* Disable interrupts */
    outb(COM1_LCR, 0x80);           /* Enable DLAB */
    outb(COM1_DATA, (uint8_t)(divisor & 0xFF));
    outb(COM1_IER, (uint8_t)(divisor >> 8));
    outb(COM1_LCR, 0x03);           /* 8N1 */
    outb(COM1_FCR, 0xC7);           /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_MCR, 0x0B);           /* DTR + RTS + OUT2 */
    return EOS_OK;
}

static int x86_uart_send(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        while (!(inb(COM1_LSR) & 0x20)) {}  /* Wait for THR empty */
        outb(COM1_DATA, p[i]);
    }
    return EOS_OK;
}

static int x86_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        uint32_t waited = 0;
        while (!(inb(COM1_LSR) & 0x01)) {   /* Wait for data ready */
            if (waited >= timeout_ms) return EOS_ERR_TIMEOUT;
            /* ~1ms busy-wait loop (approximate at 100MHz) */
            for (volatile int d = 0; d < 100000; d++) {}
            waited++;
        }
        p[i] = inb(COM1_DATA);
    }
    return EOS_OK;
}

/* ---- Timing (PIT channel 0, ~1ms tick via IRQ0) ---- */

static volatile uint32_t x86_ticks = 0;

/* Call from IRQ0 handler to increment tick count */
void board_x86_tick_handler(void)
{
    x86_ticks++;
}

static uint32_t x86_get_tick_ms(void)
{
    return x86_ticks;
}

/* ---- Reset ---- */

static eos_reset_reason_t x86_get_reset_reason(void)
{
    return EOS_RESET_POWER_ON;
}

static void x86_system_reset(void)
{
    /* Triple-fault reset: load a zero-length IDT and trigger an interrupt */
    uint8_t null_idt[6] = {0};
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile ("lidt %0; int3" : : "m"(null_idt));
#else
    (void)null_idt;
#endif
    while (1) {}
}

/* ---- Recovery pin ---- */

static bool x86_recovery_pin_asserted(void)
{
    /* No dedicated recovery pin on generic x86; could check a GPIO or key */
    return false;
}

/* ---- Jump ---- */

static void x86_jump(uint32_t vector_addr)
{
    typedef void (*entry_fn)(void);
    entry_fn entry = (entry_fn)(uintptr_t)vector_addr;
    entry();
}

/* ---- Interrupt control ---- */

static void x86_disable_interrupts(void)
{
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile ("cli" ::: "memory");
#endif
}

static void x86_enable_interrupts(void)
{
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile ("sti" ::: "memory");
#endif
}

static void x86_deinit_peripherals(void)
{
    outb(COM1_IER, 0x00);  /* Disable UART interrupts */
    outb(COM1_MCR, 0x00);  /* Deassert modem control */
}

/* ---- Early init (PIT + UART) ---- */

#define PIT_CH0_DATA  0x40
#define PIT_CMD       0x43

void board_x86_early_init(void)
{
    /* PIT channel 0: ~1ms tick (1193182 Hz / 1193 ≈ 1000 Hz) */
    uint16_t pit_div = 1193;
    outb(PIT_CMD, 0x36);                         /* Ch0, lobyte/hibyte, mode 3 */
    outb(PIT_CH0_DATA, (uint8_t)(pit_div & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)(pit_div >> 8));

    /* Initialize COM1 at 115200 baud */
    x86_uart_init(115200);
}

/* ---- Board Ops ---- */

static const eos_board_ops_t x86_ops = {
    .flash_base       = X86_FLASH_BASE,
    .flash_size       = X86_FLASH_SIZE,
    .slot_a_addr      = X86_SLOT_A,
    .slot_b_addr      = X86_SLOT_B,
    .recovery_addr    = X86_RECOVERY,
    .bootctl_addr     = X86_BOOTCTL,
    .app_vector_offset = 0x00,
    .flash_read       = x86_flash_read,
    .flash_write      = x86_flash_write,
    .flash_erase      = x86_flash_erase,
    .get_reset_reason = x86_get_reset_reason,
    .system_reset     = x86_system_reset,
    .recovery_pin_asserted = x86_recovery_pin_asserted,
    .jump             = x86_jump,
    .uart_init        = x86_uart_init,
    .uart_send        = x86_uart_send,
    .uart_recv        = x86_uart_recv,
    .get_tick_ms      = x86_get_tick_ms,
    .disable_interrupts  = x86_disable_interrupts,
    .enable_interrupts   = x86_enable_interrupts,
    .deinit_peripherals  = x86_deinit_peripherals,
};

void board_x86_init_device_table(eos_device_table_t *table)
{
    eos_device_table_init(table, "x86-GenericPC");

    table->board_id = 0x0E01;
    table->board_revision = 1;
    table->cpu_clock_hz    = 100000000;
    table->bus_clock_hz    = 33333333;
    table->periph_clock_hz = 1843200;

    eos_mem_region_t flash_region = {
        .base = X86_FLASH_BASE,
        .size = X86_FLASH_SIZE,
        .type = EOS_MEM_FIRMWARE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &flash_region);

    eos_mem_region_t ram_region = {
        .base = X86_RAM_BASE,
        .size = X86_RAM_SIZE,
        .type = EOS_MEM_USABLE,
        .attributes = 0,
    };
    eos_device_table_add_memory(table, &ram_region);

    eos_periph_entry_t uart0 = {
        .type = EOS_PERIPH_UART,
        .instance = 0,
        .base_addr = 0x3F8,
        .irq_num = 4,
        .clock_hz = 1843200,
        .flags = 0,
    };
    eos_device_table_add_peripheral(table, &uart0);
}

const eos_board_ops_t *board_x86_get_ops(void)
{
    return &x86_ops;
}
