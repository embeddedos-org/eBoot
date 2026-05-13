# Changelog

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
