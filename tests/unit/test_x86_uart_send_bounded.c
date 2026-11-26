// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_x86_uart_send_bounded.c
 * @brief x86_64_efi: the THR-empty wait in x86_uart_send must terminate.
 *
 * `x86_uart_send()` waited for the 16550 THR-empty bit with
 * `while (!(inb(COM1_BASE + 5) & 0x20)) {}` -- no bound of any kind. On a
 * build where `inb()` cannot read the port the bit never sets, so the loop
 * has no exit and any caller of `.uart_send` hangs the bootloader.
 *
 * Two such builds exist today. `CONTRIBUTING.md` lists MSVC, where the port
 * I/O `__asm` is preprocessed out because `__GNUC__` is undefined; and any
 * non-x86 host, where the `#else` stub in the board file returns a literal
 * 0. #210 fixing the `uninitvar` finding on that function made the first
 * case worse rather than better: `inb()` used to return an indeterminate
 * byte, which left the loop by accident whenever bit 5 happened to be set,
 * and now reliably returns 0, which never does.
 *
 * `x86_uart_recv()` was already bounded, by `timeout_ms`. The send path is
 * bounded by an iteration count instead: nothing in this port advances
 * `tick_ms`, so a time-based bound here would never expire either.
 *
 * Built with EBLDR_TEST_PORT_IO, which replaces only `inb`/`outb` with the
 * hooks below. Every port access in the board file goes through those two,
 * so the send logic under test is the shipping send logic. A hang shows up
 * as a ctest timeout rather than a failure, which is the correct report.
 */

/* board_x86_64_efi.c, not a model of it. */
#include "../../boards/x86_64_efi/board_x86_64_efi.c"

#include <stdio.h>
#include <stdlib.h>

#define LSR (COM1_BASE + 5)
#define LSR_THRE 0x20

/* ---- the port model the test drives ---- */

static int      thr_empty      = 0;  /* what LSR bit 5 reads back */
static unsigned lsr_reads      = 0;
static unsigned bytes_written  = 0;
static uint8_t  last_byte      = 0;
/* Set THRE only after this many reads of LSR, to model a UART that is slow
 * rather than absent. 0 means "ready immediately". */
static unsigned ready_after    = 0;

uint8_t eboot_test_inb(uint16_t port)
{
    if (port == LSR) {
        lsr_reads++;
        if (ready_after && lsr_reads >= ready_after) thr_empty = 1;
        return thr_empty ? LSR_THRE : 0x00;
    }
    return 0;
}

void eboot_test_outb(uint16_t port, uint8_t val)
{
    if (port == COM1_BASE) {
        bytes_written++;
        last_byte = val;
        /* A real 16550 clears THRE as the byte goes into the register. */
        if (ready_after) { thr_empty = 0; lsr_reads = 0; }
    }
}

static void port_reset(int ready, unsigned after)
{
    thr_empty = ready;
    lsr_reads = 0;
    bytes_written = 0;
    last_byte = 0;
    ready_after = after;
}

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

/* The defect. Before the bound this call never returned. */
TEST(send_returns_when_thr_never_empties)
{
    const uint8_t byte = 'A';
    port_reset(0, 0);
    ASSERT(x86_uart_send(&byte, 1) == EOS_ERR_TIMEOUT);
    ASSERT(bytes_written == 0);
}

TEST(send_of_many_bytes_returns_when_thr_never_empties)
{
    uint8_t buf[16];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)i;
    port_reset(0, 0);
    ASSERT(x86_uart_send(buf, sizeof(buf)) == EOS_ERR_TIMEOUT);
    ASSERT(bytes_written == 0);
}

/* The bound must not cost a working UART anything. */
TEST(send_still_delivers_when_the_uart_is_ready)
{
    const uint8_t byte = 'Z';
    port_reset(1, 0);
    ASSERT(x86_uart_send(&byte, 1) == EOS_OK);
    ASSERT(bytes_written == 1);
    ASSERT(last_byte == 'Z');
}

TEST(send_delivers_every_byte_in_order)
{
    const uint8_t buf[4] = {'e', 'B', 'o', 't'};
    port_reset(1, 0);
    ASSERT(x86_uart_send(buf, sizeof(buf)) == EOS_OK);
    ASSERT(bytes_written == 4);
    ASSERT(last_byte == 't');
}

/* A UART that makes the caller wait is served, not abandoned: the allowance
 * is re-armed for each byte, so byte 2 gets the same wait byte 1 did. */
TEST(the_spin_allowance_is_re_armed_for_each_byte)
{
    const uint8_t buf[3] = {'a', 'b', 'c'};
    port_reset(0, 64);   /* THRE sets 64 LSR reads after each byte */
    ASSERT(x86_uart_send(buf, sizeof(buf)) == EOS_OK);
    ASSERT(bytes_written == 3);
    ASSERT(last_byte == 'c');
}

/* A zero-length send touches no port, so it cannot block. */
TEST(send_of_nothing_succeeds)
{
    port_reset(0, 0);
    ASSERT(x86_uart_send(NULL, 0) == EOS_OK);
    ASSERT(lsr_reads == 0);
}

int main(void)
{
    printf("\n=== x86_64_efi UART send: the THR wait terminates ===\n");
    run_send_returns_when_thr_never_empties();
    run_send_of_many_bytes_returns_when_thr_never_empties();
    run_send_still_delivers_when_the_uart_is_ready();
    run_send_delivers_every_byte_in_order();
    run_the_spin_allowance_is_re_armed_for_each_byte();
    run_send_of_nothing_succeeds();
    printf("\n  %d/%d passed\n\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
