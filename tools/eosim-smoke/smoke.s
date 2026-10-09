@ tools/eosim-smoke/smoke.s -- minimal smoke firmware for the EoSim native engine.
@
@ The native engine (EoSim's `eosim` engine) loads a raw binary at 0x08000000
@ and fetches ARM32 from it, but decodes only a small subset (MOV, LDR, STR,
@ B, BX LR, SVC, UDF -- see eosim/engine/native/cpu.py). A full eBoot stage0
@ does hardware init the engine cannot execute, so this is the smallest
@ program the engine can honestly run to completion: set three registers,
@ then halt. It proves the sanity gate executes real instructions from a
@ real file -- not zeroed memory (eBoot#147).
@
@ Build: arm-none-eabi-as smoke.s -o smoke.o
@        arm-none-eabi-objcopy -O binary smoke.o smoke.bin
@ Run:   eosim run stm32f4 --firmware smoke.bin --headless --timeout 60
@ Expect: exit 0, "PASSED (4 cycles, halted)".
    .syntax unified
    .arm
    .global _start
    .text
    .type _start, %function
_start:
    mov r0, #0x42
    mov r1, #0x10
    mov r2, #0x20
    @ UDF with the exact encoding the engine treats as a clean halt
    @ (instr == 0xE7FFDEFE -> halted, reason "halted", success). The
    @ mnemonic form is avoided here so the encoding cannot drift with the
    @ assembler version; the three MOVs above are standard ARM32.
    .word 0xE7FFDEFE
    .size _start, . - _start
