# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

#!/usr/bin/env python3
"""
eflash.py — eBootloader Unified Flash Tool

Wraps board-vendor flashing tools behind a single CLI. Auto-detects
the correct flash tool based on the target board configuration.

Usage:
    python eflash.py flash --board stm32f4 --image build/eboot_firmware.bin
    python eflash.py list
    python eflash.py info --board nrf52
    python eflash.py check --board esp32
    python eflash.py doctor
    python eflash.py erase --board stm32f4
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Error: pyyaml is required. Install with: pip install pyyaml", file=sys.stderr)
    sys.exit(1)


CONFIGS_DIR = Path(__file__).resolve().parent.parent / "configs"
FLASH_TOOLS_YAML = CONFIGS_DIR / "flash_tools.yaml"


def load_board_configs() -> dict:
    """Load board flash configurations from flash_tools.yaml."""
    if not FLASH_TOOLS_YAML.exists():
        print(f"Error: config not found: {FLASH_TOOLS_YAML}", file=sys.stderr)
        sys.exit(1)
    with open(FLASH_TOOLS_YAML) as f:
        data = yaml.safe_load(f)
    return data.get("boards", {})


def get_board_config(boards: dict, board_name: str) -> dict:
    """Get configuration for a specific board."""
    if board_name not in boards:
        print(f"Error: unknown board '{board_name}'", file=sys.stderr)
        print(f"Available boards: {', '.join(sorted(boards.keys()))}", file=sys.stderr)
        sys.exit(1)
    return boards[board_name]


def check_tool_installed(tool_check: str) -> bool:
    """Check if a flash tool is installed and accessible."""
    if not tool_check:
        return True
    cmd = tool_check.split()[0]
    return shutil.which(cmd) is not None


def run_command(cmd: str, dry_run: bool = False) -> int:
    """Execute a shell command, or print it in dry-run mode."""
    if dry_run:
        print(f"  [dry-run] {cmd}")
        return 0
    print(f"  $ {cmd}")
    result = subprocess.run(cmd, shell=True)
    return result.returncode


def detect_board_from_build():
    """Try to auto-detect board from CMakeCache.txt in build/."""
    cache_paths = [
        Path("build/CMakeCache.txt"),
        Path("cmake-build-debug/CMakeCache.txt"),
        Path("cmake-build-release/CMakeCache.txt"),
    ]
    for cache in cache_paths:
        if cache.exists():
            for line in cache.read_text().splitlines():
                if line.startswith("EBLDR_BOARD:STRING="):
                    board = line.split("=", 1)[1].strip()
                    if board and board != "none":
                        return board
    return None


def resolve_image_path(image: str, board: str) -> str:
    """Resolve the image path, trying common build output locations."""
    if Path(image).exists():
        return image
    candidates = [
        f"build/{image}",
        "build/eboot_firmware.bin",
        "build/ebldr_stage0.bin",
        f"cmake-build-debug/{image}",
    ]
    for candidate in candidates:
        if Path(candidate).exists():
            return candidate
    return image


# ====================================================================
# Custom handlers for boards that don't use a simple CLI flash
# ====================================================================

def handle_rpi4(config: dict, image: str, args) -> int:
    """Raspberry Pi 4 — copy image to SD card."""
    mount = getattr(args, "mount", None)
    if not mount:
        system = platform.system()
        if system == "Linux":
            mount = "/media/$USER/boot"
        elif system == "Darwin":
            mount = "/Volumes/boot"
        elif system == "Windows":
            mount = "D:\\"
        else:
            mount = "/mnt/boot"
        print(f"  No --mount specified, using default: {mount}")

    mount_path = Path(os.path.expandvars(mount))
    if not mount_path.exists():
        print(f"Error: mount point not found: {mount_path}", file=sys.stderr)
        print("  Insert SD card and specify mount point with --mount", file=sys.stderr)
        return 1

    dest = mount_path / "eboot_firmware.bin"
    if getattr(args, "dry_run", False):
        print(f"  [dry-run] cp {image} -> {dest}")
        return 0

    print(f"  Copying {image} -> {dest}")
    import shutil as sh
    sh.copy2(image, dest)

    config_txt = mount_path / "config.txt"
    if not config_txt.exists():
        print("  Creating config.txt with eBoot kernel entry...")
        config_txt.write_text("kernel=eboot_firmware.bin\narm_64bit=1\n")
    else:
        content = config_txt.read_text()
        if "eboot_firmware.bin" not in content:
            print("  Warning: config.txt exists but doesn't reference eboot_firmware.bin")
            print(f"  Add 'kernel=eboot_firmware.bin' to {config_txt}")

    print("  Done! Eject SD card safely and insert into Raspberry Pi.")
    return 0


def handle_qemu(config: dict, image: str, args) -> int:
    """QEMU virtual platform — launch emulator with the image."""
    cmd = config.get("flash_command", "").replace("{image}", image)
    if not cmd:
        print("Error: no QEMU command configured for this board", file=sys.stderr)
        return 1
    print("  Launching QEMU (Ctrl+A, X to exit)...")
    return run_command(cmd, getattr(args, "dry_run", False))


CUSTOM_HANDLERS = {
    "rpi4": handle_rpi4,
    "qemu": handle_qemu,
}


# ====================================================================
# Subcommands
# ====================================================================

def cmd_list(args):
    """List all supported boards and their flash tools."""
    boards = load_board_configs()
    print(f"{'Board':<16} {'Architecture':<20} {'Flash Tool':<18} {'Interface'}")
    print(f"{'-' * 16} {'-' * 20} {'-' * 18} {'-' * 12}")
    for name in sorted(boards.keys()):
        cfg = boards[name]
        print(f"{name:<16} {cfg.get('arch', '?'):<20} {cfg.get('tool', '?'):<18} {cfg.get('interface', '?')}")
    print(f"\n{len(boards)} boards supported.")


def cmd_info(args):
    """Show detailed flash info for a board."""
    boards = load_board_configs()
    cfg = get_board_config(boards, args.board)

    installed = check_tool_installed(cfg.get("tool_check", ""))
    status = "INSTALLED" if installed else "NOT FOUND"

    print(f"Board:         {args.board}")
    print(f"Architecture:  {cfg.get('arch', '?')}")
    print(f"Flash tool:    {cfg.get('tool', '?')} [{status}]")
    print(f"Interface:     {cfg.get('interface', '?')}")
    if cfg.get("flash_address"):
        print(f"Flash address: {cfg['flash_address']}")
    print()
    if cfg.get("flash_command"):
        print(f"Flash command:")
        print(f"  {cfg['flash_command']}")
    if cfg.get("erase_command"):
        print(f"Erase command:")
        print(f"  {cfg['erase_command']}")
    if cfg.get("reset_command"):
        print(f"Reset command:")
        print(f"  {cfg['reset_command']}")
    print()
    if cfg.get("install_hint"):
        print(f"Install:  {cfg['install_hint']}")
    if cfg.get("notes"):
        print(f"Notes:    {cfg['notes']}")


def cmd_check(args):
    """Check if the required flash tool is installed for a board."""
    boards = load_board_configs()
    cfg = get_board_config(boards, args.board)

    tool = cfg.get("tool", "?")
    tool_check = cfg.get("tool_check", "")

    if not tool_check:
        print(f"Board '{args.board}' uses '{tool}' — no version check available.")
        return

    installed = check_tool_installed(tool_check)
    if installed:
        print(f"[OK] {tool} is installed for board '{args.board}'")
        try:
            result = subprocess.run(
                tool_check, shell=True, capture_output=True, text=True, timeout=5
            )
            version_line = (result.stdout or result.stderr).strip().split("\n")[0]
            if version_line:
                print(f"     {version_line}")
        except Exception:
            pass
    else:
        print(f"[MISSING] {tool} is NOT installed for board '{args.board}'")
        if cfg.get("install_hint"):
            print(f"  Install: {cfg['install_hint']}")
        sys.exit(1)


def cmd_doctor(args):
    """Check all boards and report which tools are available."""
    boards = load_board_configs()
    ready = []
    missing = []

    for name in sorted(boards.keys()):
        cfg = boards[name]
        tool = cfg.get("tool", "?")
        tool_check = cfg.get("tool_check", "")

        if not tool_check:
            ready.append((name, tool, "no check needed"))
        elif check_tool_installed(tool_check):
            ready.append((name, tool, "installed"))
        else:
            missing.append((name, tool, cfg.get("install_hint", "")))

    print("eFlash Doctor — Tool Availability Report\n")

    if ready:
        print(f"READY ({len(ready)} boards):")
        for name, tool, status in ready:
            print(f"  [OK] {name:<16} {tool} ({status})")

    if missing:
        print(f"\nMISSING ({len(missing)} boards):")
        for name, tool, hint in missing:
            print(f"  [--] {name:<16} {tool}")
            if hint:
                print(f"       Install: {hint}")

    print(f"\nTotal: {len(ready)} ready, {len(missing)} missing tools")


def cmd_flash(args):
    """Flash an image to a board."""
    boards = load_board_configs()

    board = args.board
    if not board:
        board = detect_board_from_build()
        if board:
            print(f"Auto-detected board: {board}")
        else:
            print("Error: --board is required (could not auto-detect from build/)", file=sys.stderr)
            sys.exit(1)

    cfg = get_board_config(boards, board)
    image = resolve_image_path(args.image, board)

    if not Path(image).exists():
        print(f"Error: image file not found: {image}", file=sys.stderr)
        sys.exit(1)

    # Check tool availability
    tool_check = cfg.get("tool_check", "")
    if tool_check and not check_tool_installed(tool_check):
        tool = cfg.get("tool", "?")
        print(f"Error: {tool} is not installed", file=sys.stderr)
        if cfg.get("install_hint"):
            print(f"  Install: {cfg['install_hint']}", file=sys.stderr)
        sys.exit(1)

    # Custom handler?
    handler_name = cfg.get("custom_handler")
    if handler_name and handler_name in CUSTOM_HANDLERS:
        print(f"Flashing {board} via {handler_name} handler...")
        rc = CUSTOM_HANDLERS[handler_name](cfg, image, args)
        sys.exit(rc)

    # Build flash command from template
    flash_cmd = cfg.get("flash_command", "")
    if not flash_cmd:
        print(f"Error: no flash command configured for board '{board}'", file=sys.stderr)
        print(f"  See: python eflash.py info --board {board}", file=sys.stderr)
        sys.exit(1)

    address = args.address or cfg.get("flash_address", "")
    flash_cmd = flash_cmd.replace("{image}", str(image))
    flash_cmd = flash_cmd.replace("{address}", address)
    flash_cmd = flash_cmd.replace("{board}", board)

    # Handle extra args (e.g., --port for ESP32, --device for x86)
    port = getattr(args, "port", None)
    device = getattr(args, "device", None)
    if port:
        flash_cmd = flash_cmd.replace("{port}", port)
    elif "{port}" in flash_cmd:
        system = platform.system()
        default_port = "/dev/ttyUSB0" if system == "Linux" else "COM3"
        print(f"  No --port specified, using default: {default_port}")
        flash_cmd = flash_cmd.replace("{port}", default_port)
    if device:
        flash_cmd = flash_cmd.replace("{device}", device)
    elif "{device}" in flash_cmd:
        print("Error: --device is required for this board", file=sys.stderr)
        sys.exit(1)

    image_size = Path(image).stat().st_size
    print(f"eFlash — Flashing {board}")
    print(f"  Image:  {image} ({image_size:,} bytes)")
    print(f"  Tool:   {cfg.get('tool', '?')}")
    if address:
        print(f"  Address: {address}")
    print()

    rc = run_command(flash_cmd, args.dry_run)

    if rc == 0:
        # Optional verify
        if args.verify and cfg.get("verify_command"):
            verify_cmd = cfg["verify_command"].replace("{image}", str(image))
            print("\nVerifying...")
            rc = run_command(verify_cmd, args.dry_run)

        # Optional reset
        if rc == 0 and args.reset and cfg.get("reset_command"):
            print("\nResetting device...")
            rc = run_command(cfg["reset_command"], args.dry_run)

    if rc == 0:
        print("\nFlash complete.")
    else:
        print(f"\nFlash failed (exit code {rc}).", file=sys.stderr)
    sys.exit(rc)


def cmd_erase(args):
    """Erase the flash on a board."""
    boards = load_board_configs()
    cfg = get_board_config(boards, args.board)

    erase_cmd = cfg.get("erase_command", "")
    if not erase_cmd:
        print(f"Error: no erase command configured for board '{args.board}'", file=sys.stderr)
        sys.exit(1)

    # Check tool
    tool_check = cfg.get("tool_check", "")
    if tool_check and not check_tool_installed(tool_check):
        tool = cfg.get("tool", "?")
        print(f"Error: {tool} is not installed", file=sys.stderr)
        sys.exit(1)

    # Handle extra args
    port = getattr(args, "port", None)
    if port:
        erase_cmd = erase_cmd.replace("{port}", port)
    elif "{port}" in erase_cmd:
        default_port = "/dev/ttyUSB0" if platform.system() == "Linux" else "COM3"
        erase_cmd = erase_cmd.replace("{port}", default_port)

    if not args.yes:
        print(f"WARNING: This will erase flash on {args.board}.")
        try:
            answer = input("Continue? [y/N] ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            answer = ""
        if answer != "y":
            print("Aborted.")
            sys.exit(0)

    print(f"Erasing {args.board}...")
    rc = run_command(erase_cmd, args.dry_run)
    if rc == 0:
        print("Erase complete.")
    else:
        print(f"Erase failed (exit code {rc}).", file=sys.stderr)
    sys.exit(rc)


# ====================================================================
# Main CLI
# ====================================================================

def main():
    parser = argparse.ArgumentParser(
        description="eFlash — eBootloader Unified Flash Tool",
        epilog="Run 'eflash.py <command> --help' for command-specific options.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # -- list --
    subparsers.add_parser("list", help="List all supported boards and flash tools")

    # -- info --
    info_p = subparsers.add_parser("info", help="Show flash info for a board")
    info_p.add_argument("--board", "-b", required=True, help="Target board name")

    # -- check --
    check_p = subparsers.add_parser("check", help="Check if flash tool is installed")
    check_p.add_argument("--board", "-b", required=True, help="Target board name")

    # -- doctor --
    subparsers.add_parser("doctor", help="Check all tools and report availability")

    # -- flash --
    flash_p = subparsers.add_parser("flash", help="Flash an image to a board")
    flash_p.add_argument("--board", "-b", default=None, help="Target board (auto-detects from build/ if omitted)")
    flash_p.add_argument("--image", "-i", required=True, help="Firmware image file (.bin or .eimg)")
    flash_p.add_argument("--address", "-a", default=None, help="Override flash address (hex)")
    flash_p.add_argument("--port", "-p", default=None, help="Serial port (for UART-based boards like ESP32)")
    flash_p.add_argument("--device", "-d", default=None, help="Target device (for disk-based boards like x86)")
    flash_p.add_argument("--mount", "-m", default=None, help="SD card mount point (for RPi4)")
    flash_p.add_argument("--verify", action="store_true", help="Verify after flashing")
    flash_p.add_argument("--reset", action="store_true", help="Reset device after flashing")
    flash_p.add_argument("--dry-run", action="store_true", help="Print commands without executing")

    # -- erase --
    erase_p = subparsers.add_parser("erase", help="Erase flash on a board")
    erase_p.add_argument("--board", "-b", required=True, help="Target board name")
    erase_p.add_argument("--port", "-p", default=None, help="Serial port (for UART-based boards)")
    erase_p.add_argument("--yes", "-y", action="store_true", help="Skip confirmation prompt")
    erase_p.add_argument("--dry-run", action="store_true", help="Print commands without executing")

    args = parser.parse_args()

    if args.command == "list":
        cmd_list(args)
    elif args.command == "info":
        cmd_info(args)
    elif args.command == "check":
        cmd_check(args)
    elif args.command == "doctor":
        cmd_doctor(args)
    elif args.command == "flash":
        cmd_flash(args)
    elif args.command == "erase":
        cmd_erase(args)


if __name__ == "__main__":
    main()
