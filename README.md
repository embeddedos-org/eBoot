# 🔧 eboot — EoS Bootloader

<!-- begin: org-uniform badges (audit-2026-05) -->
[![CI](https://github.com/embeddedos-org/eBoot/actions/workflows/ci.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/ci.yml)
[![CodeQL](https://github.com/embeddedos-org/eBoot/actions/workflows/codeql.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/codeql.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/embeddedos-org/eBoot/badge)](https://securityscorecards.dev/viewer/?uri=github.com/embeddedos-org/eBoot)
[![Release](https://img.shields.io/github/v/tag/embeddedos-org/eBoot?label=release&sort=semver)](https://github.com/embeddedos-org/eBoot/releases)
[![License](https://img.shields.io/github/license/embeddedos-org/eBoot)](LICENSE)
<!-- end: org-uniform badges (audit-2026-05) -->


[![CI](https://github.com/embeddedos-org/eboot/actions/workflows/ci.yml/badge.svg)](https://github.com/embeddedos-org/eboot/actions/workflows/ci.yml)
[![Nightly](https://github.com/embeddedos-org/eboot/actions/workflows/nightly.yml/badge.svg)](https://github.com/embeddedos-org/eboot/actions/workflows/nightly.yml)
[![Release](https://github.com/embeddedos-org/eboot/actions/workflows/release.yml/badge.svg)](https://github.com/embeddedos-org/eboot/actions/workflows/release.yml)
[![Version](https://img.shields.io/github/v/tag/embeddedos-org/eboot?label=version)](https://github.com/embeddedos-org/eboot/releases/latest)
[![Book](https://github.com/embeddedos-org/eBoot/actions/workflows/book-build.yml/badge.svg)](https://github.com/embeddedos-org/eBoot/actions/workflows/book-build.yml)

**Multi-platform modular bootloader with multicore, secure boot, and firmware update support**

eboot is a production-grade boot platform for embedded systems — supporting **83 board ports** across **73 architecture families**, with clean separation between boot logic, hardware abstraction, and firmware management.

→ **New to eboot?** See the [Quickstart Guide](docs/quickstart.md) — build and flash in 3 commands.

→ **Using with eos?** See the [Integration Guide](../eos/docs/integration-guide.md) — how eos + eboot + ebuild work together.

---

## ⚡ Quick Start

```bash
git clone https://github.com/embeddedos-org/eboot.git
cd eboot

# Build (native)
cmake -B build -DEBLDR_BUILD_TESTS=ON
cmake --build build
cd build && ctest

# Cross-compile for STM32F4
cmake -B build-arm -DEBLDR_BOARD=stm32f4 \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake
cmake --build build-arm

# Flash with eFlash (unified flashing tool)
python tools/eflash.py flash --board stm32f4 --image build-arm/eboot_firmware.bin --verify --reset

# Or use CMake flash target
cmake --build build-arm --target flash
```

---

## ✨ Key Features

| Category | Features |
|---|---|
| **Boot Management** | Staged boot (stage-0 + stage-1), A/B slots with automatic rollback, boot policy engine |
| **Secure Boot** | Self-contained SHA-256, CRC-32, Ed25519 signature verification, anti-rollback |
| **Firmware Update** | Stream-based pipeline (256B chunks), XMODEM/YMODEM/raw transports, pluggable custom transports |
| **Multicore** | SMP, AMP, lockstep boot; ARM PSCI, RISC-V SBI HSM, x86 SIPI, mailbox support |
| **Hardware Config** | Declarative pin muxing, memory regions, interrupt priorities, clock trees via macros |
| **RTOS Boot** | Auto-detect FreeRTOS/Zephyr/NuttX, MPU config, structured boot parameter handoff |
| **UEFI-like** | Device table, runtime services (variables, reset, time), interactive boot menu |
| **Board Registry** | Runtime multi-board selection, GCC constructor auto-registration |
| **Recovery** | UART-based recovery protocol, hardware pin trigger, factory reset |
| **Flashing** | Unified eFlash tool wrapping 25 board-vendor tools behind a single CLI |
| **Platforms** | ARM Cortex-M/A, RISC-V 32/64, Xtensa, x86_64, PowerPC, SPARC, SuperH, M68K — 83 board ports |

---

## 🏗 Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Boot Flow                             │
│  ROM → Stage-0 → Stage-1 → App (Linux / RTOS)           │
├─────────────────────────────────────────────────────────┤
│              Core Libraries (platform-agnostic)          │
│  ┌─────────┐ ┌──────────┐ ┌──────────┐ ┌────────────┐  │
│  │ Boot    │ │ Firmware │ │ Multicore│ │ Board      │  │
│  │ Control │ │ Update   │ │ Boot     │ │ Config     │  │
│  │ & Slots │ │ Pipeline │ │ SMP/AMP  │ │ Pins/Mem   │  │
│  └────┬────┘ └────┬─────┘ └────┬─────┘ └──────┬─────┘  │
│       │           │            │               │         │
│  ┌────┴───┐  ┌────┴────┐ ┌────┴─────┐  ┌─────┴──────┐  │
│  │ Crypto │  │Transport│ │ RTOS     │  │ Device     │  │
│  │ SHA-256│  │ XMODEM  │ │ Detect & │  │ Table &    │  │
│  │ CRC-32 │  │ YMODEM  │ │ Boot     │  │ Runtime Svc│  │
│  └────────┘  └─────────┘ └──────────┘  └────────────┘  │
├─────────────────────────────────────────────────────────┤
│              HAL + Board Registry                        │
│  eos_board_ops_t vtable → dispatches to active board     │
├──────┬───────┬───────┬───────┬───────┬───────┬──────────┤
│ STM32│ nRF52 │ RPi4  │ i.MX8 │RISC-V │ ESP32 │ x86_64  │
│  F4  │       │       │   M   │  virt │       │  EFI    │
│  H7  │ SAMD51│       │ AM64x │SiFive │       │         │
└──────┴───────┴───────┴───────┴───────┴───────┴──────────┘
```

---

## 📂 Repository Structure

```
eboot/
├── include/                  # Public headers (22 APIs)
├── core/                     # Core logic (platform-agnostic C)
├── hal/                      # HAL dispatch + board registry
├── stage0/                   # Minimal first-stage bootloader
├── stage1/                   # Stage-1 boot manager
├── boards/                   # Board ports (83 platforms)
│   ├── stm32f4/              #   ARM Cortex-M4 (reference)
│   ├── stm32h7/              #   ARM Cortex-M7
│   ├── nrf52/                #   ARM Cortex-M4F (BLE)
│   ├── rpi4/                 #   ARM64 Cortex-A72
│   ├── imx8m/                #   ARM64 Cortex-A53 (NXP)
│   ├── riscv64_virt/         #   RISC-V 64 (QEMU)
│   ├── esp32/                #   Xtensa LX6 (Espressif)
│   ├── x86_64_efi/           #   x86_64 UEFI
│   └── ...                   #   + 57 more
├── tests/                    # Unit tests (native host)
├── tools/                    # Host tools (eFlash, imgpack, signing)
├── configs/                  # Boot + flash config schemas (YAML)
└── docs/                     # Documentation
```

---

## 🎯 Supported Boards

| Board | Architecture | MCU / SoC | Industry Use |
|---|---|---|---|
| STM32F4 | ARM Cortex-M4 | STM32F407 | Motor control, sensors |
| STM32H7 | ARM Cortex-M7 | STM32H743 | DSP, graphics |
| nRF52 | ARM Cortex-M4F | nRF52840 | BLE wearables |
| SAMD51 | ARM Cortex-M4F | ATSAMD51 | Arduino/Adafruit IoT |
| Raspberry Pi 4 | ARM64 Cortex-A72 | BCM2711 | Edge gateways |
| NXP i.MX 8M | ARM64 Cortex-A53 | i.MX8M Mini | Industrial HMI |
| TI AM64x | ARM A53+R5F | AM6442 Sitara | PLCs, factory |
| RISC-V 64 virt | RISC-V 64 | QEMU virt | Development |
| SiFive HiFive | RISC-V U74 | FU740 | Linux eval |
| ESP32 | Xtensa LX6 | ESP32 | Wi-Fi/BLE IoT |
| x86_64 EFI | x86_64 | UEFI PCs | Edge appliances |

---

## 🖥 Multicore / Multiprocessor Boot

| Mode | Description | Example |
|---|---|---|
| **SMP** | Same firmware, shared memory | RPi4 (4× A72), ESP32 (2× Xtensa) |
| **AMP** | Different firmware per core | TI AM64x (R5F + A53) |
| **Lockstep** | Identical code, safety-critical | STM32H7 dual-core |

```c
#include "eos_multicore.h"

eos_multicore_start_smp(1, 0x08020000, 0x20010000);
eos_multicore_start_amp(0, EOS_SLOT_A, EOS_ARCH_ARM_R5);
eos_multicore_boot_all(core_configs, num_cores);
eos_multicore_wait_state(1, EOS_CORE_STATE_RUNNING, 5000);
```

---

## 📦 Firmware Update Pipeline

Stream-based — never holds the full image in RAM:

```c
#include "eos_fw_update.h"

eos_fw_update_ctx_t ctx;
eos_fw_update_begin(&ctx, EOS_SLOT_B);
while (data_available()) {
    eos_fw_update_write(&ctx, chunk, len);
}
eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST);
```

---

## 🛡 22 Core Boot Services

| # | Service | Header |
|---|---|---|
| 1 | Boot control (A/B slots) | `eos_bootctl.h` |
| 2 | Image verification | `eos_image.h` |
| 3 | Crypto (SHA-256, CRC-32) | `eos_crypto_boot.h` |
| 4 | Firmware update (stream) | `eos_fw_update.h` |
| 5 | Transport (XMODEM/YMODEM) | `eos_fw_transport.h` |
| 6 | Firmware services API | `eos_fwsvc.h` |
| 7 | Multicore boot | `eos_multicore.h` |
| 8 | RTOS-aware boot | `eos_rtos_boot.h` |
| 9 | Boot menu (UART) | `eos_boot_menu.h` |
| 10 | Device table (UEFI-style) | `eos_device_table.h` |
| 11 | Runtime services | `eos_runtime_svc.h` |
| 12 | Board config macros | `eos_board_config.h` |
| 13 | Board registry | `eos_board_registry.h` |
| 14 | Boot policy engine | `boot_policy.c` |
| 15 | Slot manager | `slot_manager.c` |
| 16 | Recovery | `recovery.c` |
| 17 | DDR/DRAM init + training | `eos_dram.h` |
| 18 | PCI/PCIe enumeration | `eos_pci.h` |
| 19 | Unified storage | `eos_storage.h` |
| 20 | Power management | `eos_power.h` |
| 21 | Clock tree init | `eos_clock.h` |
| 22 | MPU config | `eos_mpu_boot.h` |

---

## 🧪 Unit Tests

```bash
cmake -B build -DEBLDR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| Test | Covers |
|---|---|
| `test_bootctl` | Boot control block save/load, CRC, rollback |
| `test_crypto` | SHA-256 against known vectors |
| `test_device_table` | Device table create, add, validate |
| `test_runtime_svc` | Runtime variable get/set/delete |
| `test_board_config` | Pin/memory/IRQ config lookup |
| `test_multicore` | Core state management, SMP/AMP init |
| `test_board_registry` | Board register, find, activate |

---

## 🚀 CI/CD

| Workflow | Schedule | Coverage |
|----------|----------|----------|
| **CI** | Every push/PR | Build matrix (Linux × Windows × macOS) + board sanity + tests |
| **Nightly** | 2:00 AM UTC daily | Full build + test + cross-compile + regression report |
| **Weekly** | Monday 6:00 AM UTC | Comprehensive build + 6 boards + dependency audit |
| **EoSim Sanity** | 4:00 AM UTC daily | EoSim install validation (3 OS × 3 Python) + 7-platform simulation |
| **Simulation Test** | 3:00 AM UTC daily | QEMU/EoSim platform simulation across architectures |
| **Release** | Tag `v*.*.*` | Validate → cross-compile → GitHub Release with artifacts |

```bash
git tag v0.1.0
git push origin v0.1.0
```

---

## 🔐 Security

eBoot implements a secure boot chain with cryptographic verification at every stage:

### Secure Boot Chain

```
ROM → Stage-0 (minimal) → Stage-1 (full boot manager) → OS/RTOS
         │                      │
         └── CRC-32 check       └── SHA-256 hash + Ed25519 signature verify
```

### Cryptographic Primitives

| Algorithm | Implementation | Status | File |
|-----------|---------------|--------|------|
| **SHA-256** | NIST FIPS 180-4, self-contained | Real | `core/crypto_boot.c` |
| **CRC-32** | Lookup table | Real | `core/bootctl.c` |
| **Ed25519** | RFC 8032, full Curve25519 field arithmetic | Real | `core/ed25519_verify.c` |
| **Constant-time compare** | Side-channel resistant | Real | `core/crypto_boot.c` |

### Ed25519 Verification Details

The Ed25519 implementation is fully self-contained with no external dependencies:
- Full field arithmetic for Curve25519 (mod p = 2^255 - 19) using 10-limb representation
- Extended coordinates for Edwards curve point operations (add, double, scalar multiply)
- Scalar reduction mod L (group order) using Barrett reduction
- Verification: `[S]B == R + [k]A` where `k = SHA-256(R || A || M)`

> **Note**: Uses SHA-256 instead of SHA-512 internally (per project convention). The signing tool (`sign_image.py`) must use the matching algorithm.

### Anti-Rollback Protection

- Boot control block tracks firmware version monotonic counters
- A/B slot management with automatic rollback on failed boot attempts
- Maximum boot attempt policy enforcement via `boot_policy.c`
- Watchdog-based boot failure detection

### Security Features

| Feature | Description |
|---------|-------------|
| **Firmware Keystore** | Trusted key storage for signature verification (`core/keystore.c`) |
| **Debug Lock** | Hardware debug port locking for production builds (`core/debug_lock.c`) |
| **MPU Configuration** | Memory protection unit setup during boot (`core/mpu_boot.c`) |
| **Recovery Mode** | UART-based recovery with hardware pin trigger (`core/recovery.c`) |
| **Image TLV** | Type-Length-Value metadata for firmware images (`core/image_tlv.c`) |
| **Firmware Decryption** | Encrypted firmware image support (`core/fw_decrypt.c`) |

For vulnerability reports, see [SECURITY.md](SECURITY.md).

---

## Related Projects

| Project | Repository | Purpose |
|---|---|---|
| **eos** | [embeddedos-org/eos](https://github.com/embeddedos-org/eos) | Embedded OS — HAL, RTOS kernel, drivers, services |
| **ebuild** | [embeddedos-org/ebuild](https://github.com/embeddedos-org/ebuild) | Build system — YAML config, Ninja backend, packages |
| **eipc** | [embeddedos-org/eipc](https://github.com/embeddedos-org/eipc) | Inter-process communication — RPC, shared memory |
| **eai** | [embeddedos-org/eai](https://github.com/embeddedos-org/eai) | AI/ML inference runtime — on-device models |
| **eni** | [embeddedos-org/eni](https://github.com/embeddedos-org/eni) | Neural interface — BCI, assistive input |
| **eApps** | [embeddedos-org/eApps](https://github.com/embeddedos-org/eApps) | Cross-platform applications (C + LVGL) |
| **eosim** | [embeddedos-org/eosim](https://github.com/embeddedos-org/eosim) | Multi-architecture simulator |
| **EoStudio** | [embeddedos-org/EoStudio](https://github.com/embeddedos-org/EoStudio) | Design suite with LLM integration |

## Standards Compliance

This project is part of the EoS ecosystem and aligns with international standards including ISO/IEC/IEEE 15288:2023, ISO/IEC 12207, ISO/IEC/IEEE 42010, ISO/IEC 25000, ISO/IEC 25010, ISO/IEC 27001, ISO/IEC 15408, IEC 61508, ISO 26262, DO-178C, FIPS 140-3, POSIX (IEEE 1003), WCAG 2.1, and more. See the [EoS Compliance Documentation](https://github.com/embeddedos-org/.github/tree/master/docs/compliance) for full details.

## 📜 License

MIT License — see [LICENSE](LICENSE) for details.


---
Part of the [EmbeddedOS Organization](https://embeddedos-org.github.io).

<!-- begin: release-model (audit-2026-05) -->
## Release model

`master` is the line of development; every PR lands here. `release` is a
rolling pointer to the latest released `vX.Y.Z` tag, updated automatically
by [`.github/workflows/sync-release-branch.yml`](.github/workflows/sync-release-branch.yml).
Tags are immutable.

See [embeddedos-org/.github/STANDARDS.md](https://github.com/embeddedos-org/.github/blob/master/STANDARDS.md)
for the org-wide tag scheme, release model, and the compliance frameworks
every product targets.
<!-- end: release-model (audit-2026-05) -->
