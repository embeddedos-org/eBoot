# EoSim smoke firmware

A 16-byte ARM32 image for the EoSim native engine, used by the
`real-firmware` job in `.github/workflows/eosim-sanity.yml`
(eBoot#147: the sanity gate must execute real firmware, not just assert
the no-firmware contract).

The image is four instructions the engine's decoder supports
(`eosim/engine/native/cpu.py`): three `MOV`s that set registers, then a
`UDF` with the exact encoding (`0xE7FFDEFE`) the engine treats as a clean
halt. A clean halt is the engine's success contract: `eosim run` prints
`PASSED (4 cycles, halted)` and exits 0.

A full eBoot stage0 cannot run here -- it does hardware init the engine
does not model -- so this smoke image is the honest minimum: real bytes
from a real file, fetched and executed instruction by instruction.

## Build

```bash
arm-none-eabi-as tools/eosim-smoke/smoke.s -o /tmp/smoke.o
arm-none-eabi-objcopy -O binary /tmp/smoke.o /tmp/smoke.bin
```

## Run

```bash
eosim run stm32f4 --firmware /tmp/smoke.bin --headless --timeout 60
```
