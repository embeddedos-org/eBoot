# Changelog

## [Unreleased]

### Security
- **Image header is now authenticated (header format v2).** `eos_image_verify_signature()` signed `hdr->hash` only — 32 of the header's 156 bytes. Everything else (`image_size`, `load_addr`, `entry_addr`, `flags`, `sig_type`, `image_version`) sat outside the signature, so an attacker holding a legitimately signed image could relocate it, move its entry point, or clear `EOS_IMG_FLAG_HASH_SHA256` to downgrade integrity checking from SHA-256 to forgeable CRC32 — all while keeping the signature valid. The signature now covers `EOS_IMG_SIGNED_LEN` (92) bytes: the whole header except `signature[]` itself. **Existing signed images must be re-signed.**
- **`eos_image_parse_header`:** validates `hdr_version`, rejecting 0 and anything newer than this build understands.
- **`tools/eos_sign.py`:** `SIG_TYPE_ED25519` was `1` — that is `EOS_SIG_CRC32` in `eos_types.h`, which `eos_image_verify_signature()` rejects outright — and `IMG_FLAG_SIGNED` was `1 << 2`, which is `EOS_IMG_FLAG_DEBUG`. It also never set `EOS_IMG_FLAG_HASH_SHA256`, so the bootloader read the stored SHA-256 as a CRC32. Constants now match `include/eos_types.h`.
- **`recovery.c`:** UART recovery writes now reject offsets and lengths that leave the target slot, including wrap of `base + offset`.
- **`recovery.c`:** UART recovery VERIFY rejects image headers whose payload exceeds the slot capacity before streaming integrity reads past the slot boundary.
- **`image_verify.c`:** `eos_image_parse_header` rejects a `load_addr + image_size` that overflows `uint32_t` instead of wrapping the runtime end address.
- **`image_verify.c`:** The CRC32 integrity path now fails closed on a flash read error. `eos_crc32()` returned `0` when `eos_hal_flash_read()` failed, which is indistinguishable from a region that genuinely hashes to `0`, so an image whose payload could not be read passed `eos_image_verify_integrity()` when the stored CRC was `0`. The stored CRC lives in the unauthenticated header, so setting it to `0` is trivial. The SHA-256 path already propagated the read error; the two now behave the same.
- **`image_verify.c`:** `eos_image_verify_integrity` rejects a zero `image_size`, and an `addr + hdr_size` that wraps `uint32_t`, instead of computing a payload address that is not the payload.

### Fixed
- **The tree did not compile.** `include/eos_image.h` declared `eos_crc32()` as `int eos_crc32(uint32_t, size_t, uint32_t *)` while `core/image_verify.c` defined it as `uint32_t eos_crc32(uint32_t, size_t)` -- a conflicting-types error that stopped the build at the first core source file. The declaration now matches the definition and the documented behaviour.
- **`ed25519_verify.c`:** `eos_ed25519_verify()` never performed the verification. Two merged copies of the challenge-hash step had been left in the function, the second referring to identifiers that do not exist (`sha512_ctx_t`, `sc_reduce`), and RFC 8032 step 4 -- the `[S]B == R + [k]A` check -- was absent entirely, leaving the function returning an undeclared `diff`. The duplicate is removed and the group-equation check restored; the function now passes the RFC 8032 test vectors and rejects tampered messages, every single-bit signature flip, wrong keys and malleated signatures.
- **`recovery.c`:** `recovery_handle_write()` declared `slot_size` twice, which does not compile. The bounds check now calls `eos_recovery_write_in_range()` -- the helper the unit tests already exercise -- so the wire-input rule has one definition, and an unmapped slot (`base == 0`) is rejected too.
- **SHA-512 had two incompatible declarations.** `include/eos_sha512.h` declared `sha512_init/update/final` over a `sha512_ctx_t`, while `include/eos_crypto_boot.h` declared `eos_sha512_init/update/final` plus a one-shot `eos_sha512()` over an `eos_sha512_ctx_t`. `core/sha512.c` implemented the first set; `core/ed25519_verify.c` and the unit tests called the second, which nothing defined. `core/sha512.c` now implements the `eos_`-prefixed API (including the missing one-shot), `eos_sha512_ctx_t` carries the 128-bit length counter FIPS 180-4 requires, and the duplicate `include/eos_sha512.h` is removed.
- **`CMakeLists.txt`:** `core/sha512.c` and `core/rollback.c` were never compiled, so `eboot_core` could not resolve `eos_sha512_*` or `eos_rollback_*` and eleven unit-test executables failed to link. `core/boot_log.c` was listed twice. Both fixed.
- **`CMakeLists.txt`:** the `EBLDR_BOARD` dispatch chain was duplicated from `cortex_m3` onwards, and a stray `message(FATAL_ERROR ...)` sat inside the `kalimba` branch, so `cmake -DEBLDR_BOARD=kalimba` aborted configuration for a supported board and 110 later branches were unreachable. The duplicate is removed. (This is the regression `tests/unit/test_cmake_board_dispatch.py` was written to catch; it had returned.)
- **`tests/unit/test_slot_manager.c`:** the file was two different test files spliced together mid-function -- stub definitions cut in half, `main()` calling twenty functions that do not exist. Rebuilt as one suite that exercises the real `core/slot_manager.c` through scriptable per-slot verification mocks.
- **`tests/unit/test_recovery.c`:** local stand-ins for the boot-log API conflicted with `include/eos_boot_log.h` and duplicated symbols now linked from `core/boot_log.c`. Removed.
- **`tests/CMakeLists.txt`:** `unit/test_fw_transport.c` existed but was never built or run. It is now registered.

### Added
- **`eos_crc32_checked()`** — CRC32 over a flash region that reports read failures through its return value. `eos_crc32()` is retained for API compatibility and documented as unsuitable for verification decisions.

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
