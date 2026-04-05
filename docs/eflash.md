# eFlash — eBootloader Unified Flash Tool

**eFlash** wraps board-vendor flashing tools behind a single CLI. Instead of remembering vendor-specific commands for each board, use `eflash` and it auto-dispatches to the correct tool.

## Quick Start

```bash
# List all supported boards
python tools/eflash.py list

# Flash an STM32F4 board
python tools/eflash.py flash --board stm32f4 --image build/eboot_firmware.bin --verify --reset

# Flash nRF52
python tools/eflash.py flash --board nrf52 --image build/eboot_firmware.bin

# Flash ESP32 (UART-based)
python tools/eflash.py flash --board esp32 --image build/eboot_firmware.bin --port /dev/ttyUSB0

# Flash Raspberry Pi 4 (SD card)
python tools/eflash.py flash --board rpi4 --image build/eboot_firmware.bin --mount /media/user/boot

# Dry-run (show command without executing)
python tools/eflash.py flash --board stm32f4 --image build/eboot_firmware.bin --dry-run
```

## CMake Integration

When building with CMake, use the `flash` target:

```bash
cmake -B build -DEBLDR_BOARD=stm32f4 -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake
cmake --build build
cmake --build build --target flash          # Flash firmware
cmake --build build --target flash_stage0   # Flash stage-0 only
cmake --build build --target flash_info     # Show flash tool info
```

## Commands

### `list` — List Boards

```bash
python tools/eflash.py list
```

Shows all 25 supported boards with their architecture, flash tool, and interface type.

### `info` — Board Details

```bash
python tools/eflash.py info --board <name>
```

Shows the flash tool, commands, setup instructions, and installation hints for a specific board.

### `check` — Verify Tool Installation

```bash
python tools/eflash.py check --board <name>
```

Checks if the required vendor flash tool is installed and reports its version.

### `flash` — Flash Image

```bash
python tools/eflash.py flash --board <name> --image <path> [options]
```

| Flag | Description |
|------|-------------|
| `--board`, `-b` | Target board (auto-detects from `build/CMakeCache.txt` if omitted) |
| `--image`, `-i` | Firmware image file (`.bin` or `.eimg`) |
| `--address`, `-a` | Override flash address (hex) |
| `--port`, `-p` | Serial port (for UART boards like ESP32) |
| `--device`, `-d` | Target device path (for disk boards like x86) |
| `--mount`, `-m` | SD card mount point (for RPi4) |
| `--verify` | Verify image after flashing |
| `--reset` | Reset device after flashing |
| `--dry-run` | Print the flash command without executing |

### `erase` — Erase Flash

```bash
python tools/eflash.py erase --board <name> [--yes]
```

Erases the flash on the target board. Requires confirmation unless `--yes` is passed.

### `doctor` — System Audit

```bash
python tools/eflash.py doctor
```

Scans all 25 boards and reports which vendor tools are installed and which are missing, with install hints.

## Supported Boards

| Board | Architecture | Flash Tool | Interface |
|-------|-------------|-----------|-----------|
| stm32f4 | ARM Cortex-M4F | OpenOCD | ST-Link |
| stm32h7 | ARM Cortex-M7 | OpenOCD | ST-Link |
| stm32mp1 | ARM Cortex-A7 | STM32CubeProgrammer | ST-Link |
| nrf52 | ARM Cortex-M4F | nrfjprog | J-Link |
| samd51 | ARM Cortex-M4F | OpenOCD | CMSIS-DAP |
| cortex_r5 | ARM Cortex-R5 | OpenOCD | J-Link |
| rpi4 | ARM64 Cortex-A72 | SD card copy | SD card |
| imx8m | ARM64 Cortex-A53 | UUU (mfgtools) | USB |
| am64x | ARM64 Cortex-A53 | TI UniFlash | JTAG |
| qemu_arm64 | ARM64 | QEMU | Virtual |
| sifive_u | RISC-V 64 | OpenOCD | J-Link |
| riscv64_virt | RISC-V 64 | QEMU | Virtual |
| esp32 | Xtensa LX6 | esptool.py | UART |
| x86 | x86 | dd | Disk |
| x86_64_efi | x86_64 | dd | EFI |
| sparc | SPARC LEON3 | GRMON | JTAG |
| m68k | M68K | OpenOCD | BDM |
| mips | MIPS | OpenOCD | JTAG |
| powerpc | PowerPC | OpenOCD | JTAG |
| sh4 | SH-4 | OpenOCD | JTAG |
| v850 | V850 | OpenOCD | JTAG |
| frv | FR-V | GDB | JTAG |
| h8300 | H8/300 | GDB | JTAG |
| mn103 | MN103 | GDB | JTAG |
| strongarm | StrongARM | OpenOCD | JTAG |
| xscale | XScale | OpenOCD | JTAG |

## Adding a New Board

1. Add a board entry to `configs/flash_tools.yaml`:

```yaml
my_board:
  arch: arm-cortex-m33
  tool: pyocd
  tool_check: "pyocd --version"
  interface: cmsis-dap
  flash_address: "0x00000000"
  flash_command: >-
    pyocd flash -t my_mcu {image}
  erase_command: >-
    pyocd erase -t my_mcu --chip
  reset_command: >-
    pyocd reset -t my_mcu
  install_hint: "pip install pyocd"
  notes: "Connect CMSIS-DAP debug probe."
```

2. Test with `python tools/eflash.py info --board my_board`
3. Flash with `python tools/eflash.py flash --board my_board --image firmware.bin --dry-run`

## Custom Handlers

Boards that don't use a simple CLI flash command (like RPi4's SD card copy or QEMU's emulator launch) use custom Python handlers defined in `eflash.py`. Add the `custom_handler` key in `flash_tools.yaml` to route to a handler function.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "tool is not installed" | Run `eflash.py check --board <name>` for install instructions |
| "image file not found" | Build first: `cmake --build build` |
| Flash fails on STM32 | Ensure ST-Link is connected and drivers installed |
| ESP32 won't enter bootloader | Hold BOOT button while pressing RESET, then flash |
| RPi4 won't boot | Check `config.txt` has `kernel=eboot_firmware.bin` |
| QEMU exits immediately | Ensure image is a valid ELF or raw binary for the target arch |

## Requirements

- Python 3.10+
- `pyyaml` (`pip install pyyaml`)
- Board-specific vendor tool (run `eflash.py doctor` to check)
