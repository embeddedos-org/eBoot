# Changelog

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
