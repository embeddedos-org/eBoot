/**
 * STM32F407 Linker Script for eBootloader Stage-0
 *
 * Stage-0 occupies the first 16K of flash.
 * RAM: 128K SRAM (0x20000000 - 0x2001FFFF)
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* ARM exception-unwinding tables. Nothing in this image unwinds: it is
     * C with no exceptions, linked -nostartfiles, and no _Unwind_* or
     * __exidx_* symbol is referenced anywhere in it. Left unplaced,
     * .ARM.exidx is an orphan section, and ld's orphan placement put it in
     * FLASH just after .text -- 8 bytes of the 16 KiB stage-0 budget spent
     * on a table no code can reach, and spent silently. It comes in from
     * newlib's memcpy.o, so building our own sources with
     * -fno-unwind-tables would not remove it; only the link can. Discard it
     * here rather than let orphan placement put it in a full region. */
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}
