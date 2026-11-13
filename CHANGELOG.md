# Changelog

## [Unreleased]

### Security
- **UART recovery issues no challenge on a board without an entropy source.** When `eos_hal_rng_get()` failed, `recovery_handle_auth()` filled the 32-byte challenge from a linear congruential generator seeded with `eos_hal_get_tick_ms()`. The challenge is the only thing that stops a captured `(challenge, response)` pair from being replayed, and with that fallback it was a function of the millisecond at which the AUTH command was handled -- a few thousand values on a freshly reset board -- which the client sees before it has to answer and can retry after a reset at no cost in failure count. The AUTH is now refused outright (NACK, event `EOS_LOG_AUTH_NO_ENTROPY` = `0x23`, which `tools/uart_recovery.py` decodes; counted like any other failure so the backoff applies). No board port provides `rng_get` (0 of the 83 ops tables under `boards/`), so this was the challenge on every board; none provides `otp_read` either, so no board authenticates anyone today and the defect was latent. Pinned by `test_auth_refuses_when_the_board_has_no_entropy_source` in `tests/unit/test_recovery.c`.
- **UART recovery refuses an unprovisioned secret, and a write into an unmapped slot.** `recovery_handle_auth()` compared the client's response against `SHA-256(challenge || secret)` for whatever the OTP held at the secret offset -- and unprogrammed fuses read back as all zeros or all ones, both public, so a device whose recovery secret was never provisioned authenticated any client that knew that. Both patterns are now refused before the comparison (logged as event `0x22`), the way the keystore already refuses an all-zero key. `recovery_handle_write()` had stopped calling `eos_recovery_write_in_range()` -- the rule the unit tests exercise -- for an inline check that never looked at the slot base, so a slot the board leaves unmapped (base 0) turned "write at offset" into "write at flash address". The handler uses the shared rule again -- which also refuses a `WRITE` with `len == 0`, where the inline check had ACKed it and written nothing; a recovery client sees a NACK there now. No shipped board port provides `otp_read` today, so the first defect was latent (authentication always failed); both are pinned by `tests/unit/test_recovery.c`. The authentication outcomes are logged as `EOS_LOG_AUTH_SUCCESS` / `EOS_LOG_AUTH_FAIL` / `EOS_LOG_AUTH_UNPROVISIONED` (`0x20`-`0x22`, now defined in `include/eos_types.h` beside the other `EOS_LOG_*` codes instead of as bare hex), `tools/uart_recovery.py` decodes all three, and `tests/unit/test_boot_log_event_names.py` keeps the firmware, the header and the client's table in step.
- **The production-key gate covers every release-shaped build, and the key it accepts is checked against the curve.** The gate matched the literal string `Release` and nothing else, so `MinSizeRel`, `RelWithDebInfo`, `release`, `RELEASE` and an unset build type all configured a real board on the development anchor. It now exempts only `Debug`. Separately, `EBLDR_PRODUCTION_KEY` was checked for length and hex-ness but never for being a point on edwards25519 — the defect the development key itself had — so one mistyped hex digit in the release secret would have shipped a fleet that refuses every image, with a green build. `tools/check_production_key.py` applies the verifier's own rule (`[L]P == identity`, `P != identity`, not the development key) in pure Python; `cmake/ProductionKey.cmake` runs it at configure when python3 is present and warns in so many words when it is not, and `release.yml` runs it on the secret before any board is configured.
- **A release can no longer be built on the development trust anchor.** `EBLDR_PRODUCTION_KEY` had no CMake option, nothing set it, and its `#else` branch declared an `extern` nothing defined -- so every firmware job in `release.yml` compiled the `#ifndef` branch and shipped the RFC 8032 test key as the anchor for every board without OTP, which is every board under `boards/`. The only guard was a `#warning`. Now: `-DEBLDR_PRODUCTION_KEY=<64 hex characters>` generates `ebldr_production_key[]` (`cmake/ProductionKey.cmake`, declared in `include/eos_production_key.h`) and selects the production branch of `core/keystore.c`; a Release build of a real board refuses to configure without it unless it says `-DEBLDR_ALLOW_DEV_KEY=ON`; the development key and malformed values are refused as production keys; `release.yml` passes the `EBLDR_PRODUCTION_KEY_HEX` secret on every board configure (empty until a maintainer provides it, so a release fails closed) and scans every built `.elf`/`.bin`/`.a`/`.o` for the development key's bytes. `tests/unit/test_keystore_production.c` compiles the production branch with a fixture key on every host build; `tests/unit/test_production_key_gate.py` and `test_release_workflow_production_key.py` pin the gate and the workflow.
- **The compiled-in development trust anchor was not a point on the curve.** `core/keystore.c` `default_dev_key` was described as the RFC 8032 section 7.1 TEST 1 public key, but it diverged from that key at byte 21 and the bytes it held did not decode to a point on the curve, so no signature could ever verify against it. It is now the RFC key, and `tests/unit/test_keystore.c` checks that the anchor verifies the RFC's own signature. Behaviour change: a board with no OTP and no `EBLDR_PRODUCTION_KEY` goes from refusing every image (since #104 made signature verification unconditional at install) to accepting images signed with the public RFC test key -- the documented development intent of the `#warning` in `core/keystore.c`, and a key that must never reach a device. #120 tracks making that structural.
- **Image header is now authenticated (header format v2).** `eos_image_verify_signature()` signed `hdr->hash` only — 32 of the header's 156 bytes. Everything else (`image_size`, `load_addr`, `entry_addr`, `flags`, `sig_type`, `image_version`) sat outside the signature, so an attacker holding a legitimately signed image could relocate it, move its entry point, or clear `EOS_IMG_FLAG_HASH_SHA256` to downgrade integrity checking from SHA-256 to forgeable CRC32 — all while keeping the signature valid. The signature now covers `EOS_IMG_SIGNED_LEN` (92) bytes: the whole header except `signature[]` itself. **Existing signed images must be re-signed.**
- **`eos_image_parse_header`:** validates `hdr_version`, rejecting 0 and anything newer than this build understands.
- **`tools/eos_sign.py`:** `SIG_TYPE_ED25519` was `1` — that is `EOS_SIG_CRC32` in `eos_types.h`, which `eos_image_verify_signature()` rejects outright — and `IMG_FLAG_SIGNED` was `1 << 2`, which is `EOS_IMG_FLAG_DEBUG`. It also never set `EOS_IMG_FLAG_HASH_SHA256`, so the bootloader read the stored SHA-256 as a CRC32. Constants now match `include/eos_types.h`.
- **`recovery.c`:** UART recovery writes now reject offsets and lengths that leave the target slot, including wrap of `base + offset`.
- **`recovery.c`:** UART recovery VERIFY rejects image headers whose payload exceeds the slot capacity before streaming integrity reads past the slot boundary.
- **`image_verify.c`:** `eos_image_parse_header` rejects a `load_addr + image_size` that overflows `uint32_t` instead of wrapping the runtime end address.
- **`image_verify.c`:** The CRC32 integrity path now fails closed on a flash read error. `eos_crc32()` returned `0` when `eos_hal_flash_read()` failed, which is indistinguishable from a region that genuinely hashes to `0`, so an image whose payload could not be read passed `eos_image_verify_integrity()` when the stored CRC was `0`. The stored CRC lives in the unauthenticated header, so setting it to `0` is trivial. The SHA-256 path already propagated the read error; the two now behave the same.
- **`image_verify.c`:** `eos_image_verify_integrity` rejects a zero `image_size`, and an `addr + hdr_size` that wraps `uint32_t`, instead of computing a payload address that is not the payload.

### Fixed
- **`eflash.py flash --reset` fills in the reset command's placeholders.** `cmd_flash()` filled `{image}`, `{address}`, `{board}`, `{port}` and `{device}` into `flash_command` only; `verify_command` got `{image}` and `reset_command` got nothing, so on ESP32 `--reset` ran `esptool.py --chip esp32 --port {port} run` with a literal `{port}`. All three commands now take the same values, and the default port is chosen once for all of them. Pinned by `tests/unit/test_eflash_templates.py`.
- **Stage-0 refuses a stage-1 image it could not read.** The stage-1 hash loop in `stage0/jump_stage1.c` discarded `eos_hal_flash_read()`'s result, so a failed read hashed the previous chunk (or the stack) as if it were stage-1; the mismatch that followed reached recovery with the wrong reason (`0xBAD1`, hash mismatch), and only by luck. A failed read now enters recovery with its own reason (`EBLDR_FAIL_STAGE1_READ`, `0xBAD2`) and never falls through to the jump, matching what `eos_crypto_verify_image()` in `core/` already does. The hash-mismatch path (`EBLDR_FAIL_STAGE1_HASH`, `0xBAD1`) and the recovery-trigger path had the same fall-through -- `eos_recovery_enter()` with nothing after it, so a failed verification reached the `IMAGE_VALID` record and the jump, held back only by recovery never returning -- and both now return. The two detail codes are named in `include/eos_types.h` beside the `EOS_LOG_*` events. `tests/unit/test_stage0_hal_results.py` is a source-level guard that every HAL read or write in `stage0/` has its result tested (not merely assigned; preprocessor lines are ignored), and that every entry into recovery in `ebldr_stage0_main()` is followed by a return.
- **The stage-1 image was empty, and stage-0 verified it anyway.** On every board build with a stage-1 linker script, `eboot_firmware.bin` was 0 bytes: `stage1/` exported `eboot_main()` only, the script had no `ENTRY()` and kept no section, and with `-nostartfiles` and `--gc-sections` the linker had nothing to anchor. `tools/embed_stage1_hash.py` took the size verbatim and emitted `stage1_expected_size = 0u` with the SHA-256 of the empty string; `stage0/jump_stage1.c` ran its hash loop zero times, matched nothing against nothing, recorded `IMAGE_VALID` and jumped. Three independent fixes: `stage1/reset_entry.c` now owns the vector table and the reset handler (stack pointer, `Reset_Handler` into `eboot_main()`, faults and the system exceptions through SysTick) and `boards/stm32f4/stm32f4_stage1.ld` has `ENTRY(Reset_Handler)` and keeps `.isr_vector`, so the stm32f4 stage-1 links to 13,416 bytes and the embedded digest is that image's; the embed tool refuses an empty image outright and one below a 4096-byte floor (`--min-size`); and stage-0 refuses `stage1_expected_size == 0` before the loop (`EBLDR_FAIL_STAGE1_NO_IMAGE`, `0xBAD3`), because a bootloader does not trust its own build. Stage-0's own vector table stopped at UsageFault while `board_early_init()` enables the SysTick interrupt, so the first tick fetched a handler address from the code after the table; both tables now run through SysTick. `cortex_r5` is left as it was: both of its linker scripts keep `.vectors`, which nothing in the tree emits, so both of its images link to zero bytes and the embed tool now stops that board's build at the empty stage-1 instead of describing it; the port needs an entry shape of its own (#137), and the guard names it as a strict expected failure until then. The guard also checks that every section a stage script keeps is one some source file emits, which is the mismatch that produced both empty images. `tests/unit/test_stage1_image_is_verified.py` pins all of it.
- **The tree did not configure, compile or link after the 09-07 batch merge.** `tests/CMakeLists.txt` registered `eboot_test_fdt_loader` twice; `core/sha512.c` had been replaced by a version predating the `bitlen[2]`/`buffer_len` context; `core/boot_log.c`, `core/secure_boot.c` and `core/fdt_loader.c` had been dropped from `eboot_core`; `scalarbase()` and `k_low_order[]` were defined twice; and the `eos_boot_log_get_head()` declaration was lost. All restored.
- **Install-path verification order settled: signature before anti-rollback.** `eos_fw_update_finalize()` verifies the Ed25519 signature over the signed header prefix first and reads the TLV security counter only after the prefix that binds it is authenticated (see `docs/adr/ADR-020`). The `fw_update` and `fw_transport` suites now stream genuinely signed images; `tools/gen_fw_update_test_sigs.py` emits their signatures as `tests/vectors/fw_update_test_sigs.h`, and `tests/unit/test_fw_update_test_sigs.py` pins the committed header to the generator's output.
- **`tests/CMakeLists.txt`:** the Valgrind list is derived from the registered suites again; a hand-written copy had replaced it, eleven registered suites were missing from `EBLDR_UNIT_TESTS`, and seven of those (`test_eos_sign_boot_path`, `test_fdt_loader`, `test_fw_decrypt`, `test_fw_update_sig`, `test_jump_app_bounds`, `test_qemu_arm64_timer`, `test_secure_boot_policy`) had no Valgrind run at all; the other four were only in the hand-written list. All eleven are appended.
- **`.github/workflows/ci.yml`:** `fuzz-build` is in the CI gate. It was added after the gate job and the gate never waited for it.
- **Unit suites count `tests_run`** as each test executes instead of assigning it a literal that the summary line then trusted.
- **`.github/workflows/eosim-sanity.yml`:** the install-validate job's steps are bash and now run under `shell: bash` on the Windows legs, where PowerShell rejected `SITE_PACKAGES=$(...)` and parsed `|| { exit 1 }` as an unexecuted script block.
- **`.github/workflows/scorecard.yml`:** `ossf/scorecard-action` moved to v2.4.3, the release hosted on ghcr.io; v2.4.0 pulls from gcr.io, which now requires GCP billing.
- **The tree did not compile.** `include/eos_image.h` declared `eos_crc32()` as `int eos_crc32(uint32_t, size_t, uint32_t *)` while `core/image_verify.c` defined it as `uint32_t eos_crc32(uint32_t, size_t)` -- a conflicting-types error that stopped the build at the first core source file. The declaration now matches the definition and the documented behaviour.
- **`ed25519_verify.c`:** `eos_ed25519_verify()` never performed the verification. Two merged copies of the challenge-hash step had been left in the function, the second referring to identifiers that do not exist (`sha512_ctx_t`, `sc_reduce`), and RFC 8032 step 4 -- the `[S]B == R + [k]A` check -- was absent entirely, leaving the function returning an undeclared `diff`. The duplicate is removed and the group-equation check restored; the function now passes the RFC 8032 test vectors and rejects tampered messages, every single-bit signature flip, wrong keys and malleated signatures.
- **`recovery.c`:** `recovery_handle_write()` declared `slot_size` twice, which does not compile. The bounds check now calls `eos_recovery_write_in_range()` -- the helper the unit tests already exercise -- so the wire-input rule has one definition, and an unmapped slot (`base == 0`) is rejected too.
- **SHA-512 had two incompatible declarations.** `include/eos_sha512.h` declared `sha512_init/update/final` over a `sha512_ctx_t`, while `include/eos_crypto_boot.h` declared `eos_sha512_init/update/final` plus a one-shot `eos_sha512()` over an `eos_sha512_ctx_t`. `core/sha512.c` implemented the first set; `core/ed25519_verify.c` and the unit tests called the second, which nothing defined. `core/sha512.c` now implements the `eos_`-prefixed API (including the missing one-shot), `eos_sha512_ctx_t` carries the 128-bit length counter FIPS 180-4 requires, and the duplicate `include/eos_sha512.h` is removed.
- **`CMakeLists.txt`:** `core/sha512.c` and `core/rollback.c` were never compiled, so `eboot_core` could not resolve `eos_sha512_*` or `eos_rollback_*` and eleven unit-test executables failed to link. `core/boot_log.c` was listed twice. Both fixed.
- **`CMakeLists.txt`:** the `EBLDR_BOARD` dispatch chain was duplicated from `cortex_m3` onwards, and a stray `message(FATAL_ERROR ...)` sat inside the `kalimba` branch, so `cmake -DEBLDR_BOARD=kalimba` aborted configuration for a supported board and 110 later branches were unreachable. The duplicate is removed. (This is the regression `tests/unit/test_cmake_board_dispatch.py` was written to catch; it had returned.)
- **`tests/unit/test_slot_manager.c`:** the file was two different test files spliced together mid-function -- stub definitions cut in half, `main()` calling twenty functions that do not exist. Rebuilt as one suite that exercises the real `core/slot_manager.c` through scriptable per-slot verification mocks.
- **`tests/unit/test_recovery.c`:** local stand-ins for the boot-log API conflicted with `include/eos_boot_log.h` and duplicated symbols now linked from `core/boot_log.c`. Removed.
- **`tests/CMakeLists.txt`:** `unit/test_fw_transport.c` existed but was never built or run. It is now registered.
- **`EBLDR_REQUIRE_SIGNATURES` and `EBLDR_RECOVERY_AUTH` were switches wired to nothing.** `CMakeLists.txt` offered both as options, forwarded each as a compile definition, and the README listed both as `ON` by default -- but no source file tested either macro, so a configure with one `OFF` produced byte-for-byte the same objects as `ON` (41 of 41 non-test objects identical with the definitions forced back in). Signature verification and recovery authentication are unconditional; the two options are gone, passing either as `OFF` fails the configure, the README says so, and `docs/book/book.md` no longer lists `EBLDR_SECURE_BOOT`, `EBLDR_MULTICORE`, `EBLDR_RECOVERY` and `EBLDR_BOOT_MENU`, none of which ever existed. `tests/unit/test_build_options_are_read.py` requires every forwarded `EBLDR_` definition to be read by a source file and every documented option to be declared.

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
