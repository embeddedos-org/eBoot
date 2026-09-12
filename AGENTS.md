# AGENTS.md — eBoot

eBoot (CMake project `eBootloader`, v3.0.2) is a multi-platform, modular secure
bootloader written in C: a minimal Stage-0 (early hardware bring-up) plus a
Stage-1 that scans, selects, verifies (Ed25519), and jumps to an application or
RTOS image, with A/B slot management, recovery, and firmware-update transports.
(Provenance: `README.md` intro.)

## Layout (from `README.md` "What's inside")

- `stage0/` — reset entry, hardware init, watchdog, recovery entry, jump to Stage-1
- `stage1/` — boot logic: scan, select, boot log, jump to app (`main.c`)
- `core/` — platform-agnostic boot logic (Ed25519 verify, image TLV/verify, slot
  manager, keystore, anti-rollback, firmware update/decrypt, UART transport,
  boot policy/menu, recovery); builds `eboot_core`
- `hal/` — HAL dispatch and board registry; builds `eboot_hal`
- `include/` — public headers (`eos_secure_boot.h`, `eos_image.h`, …)
- `boards/` — per-architecture board support
- `configs/` — boot/flash/image YAML schemas and flash-tool config
- `toolchains/` — cross-compile toolchain files (aarch64, arm-none-eabi, riscv64, …)
- `tests/` — `unit/`, `functional/`, `fuzz/`, `performance/`, `simulation/`
- `docs/` — quickstart, architecture, secure boot chain, threat model, etc.

## Build (from `README.md` "Build" and `CONTRIBUTING.md` "Development Setup")

Requires CMake ≥ 3.15 and a C compiler. Native build compiles the
platform-agnostic core libraries (no architecture-specific code; works on
Linux, macOS, Windows). Tests are disabled by default; enable with
`EBLDR_BUILD_TESTS=ON`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEBLDR_BUILD_TESTS=ON
cmake --build build --parallel
```

Cross-compile for a target board with `EBLDR_BOARD` plus a toolchain file:

```bash
cmake -B build/stm32 -DEBLDR_BOARD=stm32f4 \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake
cmake --build build/stm32 --parallel
```

## Test (from `README.md` "Test" and `CONTRIBUTING.md` "Test Suites")

```bash
cmake -S . -B build -DEBLDR_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python run_all_tests.py
```

Python tooling setup: `pip install -r requirements.txt`. Config-generation
check: `python scripts/generate_config.py configs/example_boot.yaml /tmp/generated/`.

## Lint / format

Not defined: no lint or format command is documented in `README.md` or
`CONTRIBUTING.md`. (A `.clang-tidy` config file exists in the tree, but the
repo documents no command to invoke it; C style rules — C11, `-Wall -Wextra`
clean — are in `CONTRIBUTING.md` "Code Guidelines".)

## Contributing

See `CONTRIBUTING.md`: fork, create a feature branch (`git checkout -b
feat/my-feature`), run the build and tests locally, then submit a pull request.
Follow Conventional Commits; new features need unit tests in `tests/unit/`
(registered with both `add_executable()` and `add_test()` in
`tests/CMakeLists.txt`).

## Security

See `SECURITY.md`. Report vulnerabilities to security@embeddedos.org — do NOT
open public issues for vulnerabilities. Response SLA: acknowledgment within
48 hours; 90-day coordinated disclosure.
