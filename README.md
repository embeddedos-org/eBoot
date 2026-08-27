# eBoot — Secure Bootloader

[![CI](https://github.com/embeddedos-org/eBoot/actions/workflows/ci.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/ci.yml)
[![CodeQL](https://github.com/embeddedos-org/eBoot/actions/workflows/codeql.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/codeql.yml)
[![Scorecard](https://github.com/embeddedos-org/eBoot/actions/workflows/scorecard.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/scorecard.yml)
[![Release](https://github.com/embeddedos-org/eBoot/actions/workflows/release.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

eBoot (CMake project `eBootloader`) is a multi-platform, modular secure
bootloader written in C. It is organized as a two-stage boot chain: a minimal
Stage-0 that performs early hardware bring-up, and a Stage-1 that scans, selects,
verifies, and jumps to an application or RTOS image. Image authentication uses
Ed25519 signatures, with A/B slot management, recovery, and firmware-update
transports built into the core.

eBoot is the boot component of the **EmbeddedOS**
([embeddedos-org](https://github.com/embeddedos-org)) ecosystem, providing
verified boot for [EoS](https://github.com/embeddedos-org/eos) and other
payloads. Project version 3.0.2.

## What's inside

| Path | Contents |
|------|----------|
| `stage0/` | Minimal early boot: reset entry, hardware init, watchdog, recovery entry, jump to Stage-1 |
| `stage1/` | Boot logic: scan, select, boot log, jump to app (`main.c`) |
| `core/` | Platform-agnostic boot logic — Ed25519 verify, image TLV/verify, slot manager, secure/RTOS boot, keystore, anti-rollback counter, firmware update/decrypt, UART transport, boot policy/menu, recovery — builds `eboot_core` |
| `hal/` | HAL dispatch and board registry — builds `eboot_hal` |
| `include/` | Public headers (`eos_secure_boot.h`, `eos_image.h`, `eos_slot_manager.h`, `eos_keystore.h`, …) |
| `boards/` | Per-architecture board support (83 board directories) |
| `configs/` | Boot/flash/image YAML schemas and flash-tool config |
| `toolchains/` | Cross-compile toolchain files (aarch64, arm-none-eabi, riscv64, …) |
| `tests/` | `unit/`, `functional/`, `fuzz/`, `performance/`, `simulation/` |

## Build

Requires CMake ≥ 3.15 and a C compiler. A native build (no board selected)
compiles the platform-agnostic core libraries and, with tests enabled, the test
suite — these contain no architecture-specific code and build on Linux, macOS,
and Windows.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Cross-compile for a target board with `EBLDR_BOARD` and a toolchain file:

```bash
cmake -B build/stm32 \
  -DEBLDR_BOARD=stm32f4 \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake
cmake --build build/stm32 --parallel
```

`EBLDR_BOARD` accepts `stm32f4`, `stm32h7`, `nrf52`, `rpi4`, `riscv64_virt`,
`esp32`, `x86_64_efi`, `imx8m`, `am64x`, `samd51`, `sifive_u`, `cortex_r5`, and
others (default `none` = native core-only build).

### Security options

| Option | Default | Meaning |
|--------|---------|---------|
| `EBLDR_REQUIRE_SIGNATURES` | `ON` | Require Ed25519 signatures for boot |
| `EBLDR_VERIFY_STAGE1` | `ON` | Verify the Stage-1 hash before jumping |
| `EBLDR_RECOVERY_AUTH` | `ON` | Require authentication for recovery commands |
| `EBLDR_HARDENING` | `ON` | Compiler hardening (`-fstack-protector-strong`, `_FORTIFY_SOURCE=2`) |
| `EBLDR_SANITIZE` | `OFF` | ASan/UBSan for host builds |
| `EBLDR_BUILD_FUZZ` | `OFF` | Build libFuzzer targets |
| `EBLDR_BUILD_TESTS` | `OFF` | Build unit tests (native only) |

## Test

```bash
cmake -B build -DEBLDR_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Python-driven suites
python run_all_tests.py
```

## Docs

See [`docs/`](docs/): `quickstart.md`, `architecture.md`, `secure_boot_chain.md`,
`key_lifecycle.md`, `threat_model.md`, `memory-map.md`, `update-flow.md`, and the
`security_review_checklist.md`.

## License

Licensed under the [MIT License](LICENSE).
