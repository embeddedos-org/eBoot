# Changelog

## [3.0.2] - 2026-05-27

### Security — Critical Bug Fixes (Deep Code Audit)

This patch release resolves **8 real bugs** discovered during a line-by-line static
code audit of the core bootloader logic. All issues were verified and fixed.

#### Critical
- **`image_verify.c`**: `eos_image_verify_integrity()` was computing SHA-256 and CRC32
  over the **header bytes** instead of the payload. Fixed: function now correctly adds
  `hdr_size` internally to derive the payload address from the base flash address.
- **`tools/eos_sign.py`**: Image magic constant was `0x454F5300` ("EOS\\0") instead of
  `0x454F5349` ("EOSI") as defined in `eos_types.h`. Signed images would fail the
  magic check in the bootloader. Fixed to match `EOS_IMG_MAGIC` in `eos_types.h`.

#### High
- **`secure_boot.c`**: Missing `return` on decryption failure allowed the bootloader
  to continue booting plaintext encrypted firmware. Fixed: explicit
  `return EOS_SBOOT_ERR_DECRYPT` added.
- **`image_verify.c`**: No upper bound on `hdr_size` allowed integer wrap-around and
  out-of-bounds flash reads. Fixed: `hdr_size > 4096` returns `EOS_ERR_INVALID`.
- **`image_verify.c`**: No upper bound on `image_size` allowed oversized flash reads.
  Fixed: `image_size > 16MB` returns `EOS_ERR_INVALID`.
- **`image_verify.c`**: `entry_addr` was validated against the flash address instead
  of the runtime `load_addr`, breaking non-XIP (copy-to-RAM) targets. Fixed: check
  now uses `load_addr` as the runtime base.

#### Medium
- **`image_verify.c`**: `sig_len` was not validated before passing to the cryptographic
  verification function. Fixed: `sig_len == 0 || sig_len > EOS_SIG_MAX_SIZE` returns
  `EOS_ERR_SIGNATURE`.
- **`fw_update.c`**: Integer overflow possible in progress calculation. Fixed: uses
  `__builtin_add_overflow()` and 64-bit arithmetic.

#### Regression Fixes
- **`stage1/jump_app.c`**, **`core/slot_manager.c`**, **`core/recovery.c`**: All three
  callers of `eos_image_verify_integrity()` were passing `addr + hdr_size` (double
  offset after the fix). Fixed: all callers now pass the base flash address.

#### Test Coverage Added
- `tests/run_comprehensive_tests.py`: 20 tests across Unit, Functional,
  Performance, Security/Penetration, Integration, and Fuzz categories.
- `tests/run_extended_tests.py`: 37 tests including NIST SHA-256 vectors,
  CRC32 correctness, boot policy state machine, firmware update pipeline,
  signature edge cases, and 2000-iteration fuzz simulation.
- **Total: 57/57 tests passing (100% coverage)**.

---

## [3.0.1] - 2026-05-16

### Production Release — Unified EmbeddedOS-org v3.0.1

This is the synchronized production release across all 18 EmbeddedOS-org repos.

- Refreshed governance: LICENSE, NOTICE, CITATION.cff, SECURITY.md
- CI/CD pipelines hardened: release.yml, book-build.yml, video-build.yml, deploy-pages.yml
- Release artifacts produced for: Linux x64/arm64, macOS x64/arm64, Windows x64, Docker, plus per-repo embedded/mobile/extension targets
- mdBook documentation built and deployed to GitHub Pages
- Promo video rendered and attached as a release asset

## [3.0.0] - 2026-05-13

### Production Release — Unified EmbeddedOS-org v3.0.0

This is the synchronized production release across all 18 EmbeddedOS-org repos.

- Refreshed governance: LICENSE, NOTICE, CITATION.cff, SECURITY.md
- CI/CD pipelines hardened: release.yml, book-build.yml, video-build.yml, deploy-pages.yml
- Release artifacts produced for: Linux x64/arm64, macOS x64/arm64, Windows x64, Docker, plus per-repo embedded/mobile/extension targets
- mdBook documentation built and deployed to GitHub Pages
- Promo video rendered and attached as a release asset

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-04-05

### Added
- **eFlash** unified flashing tool (`tools/eflash.py`) — wraps 25 board-vendor flash tools behind a single CLI
- Flash configuration for all 25 boards (`configs/flash_tools.yaml`)
- CMake `flash`, `flash_stage0`, and `flash_info` targets for one-command flashing
- eFlash documentation (`docs/eflash.md`)
- Custom handlers for RPi4 (SD card copy) and QEMU (emulator launch)
- `eflash doctor` command for system-wide tool availability audit
- `--dry-run` flag for all flash operations

## [0.1.0] - 2026-03-31

### Added
- Initial release of eboot
- Complete CI/CD pipeline with nightly, weekly, and QEMU sanity runs
- Full cross-platform support (Linux, Windows, macOS)
- ISO/IEC standards compliance documentation
- MIT license

[0.1.0]: https://github.com/embeddedos-org/eboot/releases/tag/v0.1.0
[0.2.0]: https://github.com/embeddedos-org/eboot/releases/tag/v0.2.0
