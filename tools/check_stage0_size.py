#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project

"""Gate the stm32f4 stage-0 flash headroom (eBoot #207).

`ebldr_stage0.elf` links into a 16 KiB flash region
(`boards/stm32f4/stm32f4_stage0.ld`). On master at #207 filing it had
12 bytes free under CI's toolchain (arm-none-eabi-gcc 14.2.1), so a
16-byte change failed to link with `region FLASH overflowed` -- and the
failure mode was a cryptic linker error, not a message saying what the
limit is. This script prints the figure and fails with a clear message
when the free space drops under a stated minimum, so the limit is
visible before the linker error.

Flash use is `text + data` from `arm-none-eabi-size` (Berkeley format):
`.text` and `.rodata` live in flash, `.data`'s load image lives in
flash, `.bss` is RAM-only. The budget is the region length from the
linker script; the minimum free space is the policy: 16 bytes, set
from the measured headroom at landing (12 bytes free on master, +8
from discarding the unreachable `.ARM.exidx` in the stage-0 linker
script, +16 from keeping Reset_Handler's copy loops as loops). Raise
it when more room is reclaimed; do not lower it to make a change fit --
a change that does not fit needs a slimmer verify path or a larger
stage-0 (moving the stage-0/stage-1 boundary, which needs its own
decision), not a smaller number here.

Usage (from the Cross-compile STM32F4 job in .github/workflows/build.yml):

    python3 tools/check_stage0_size.py --elf build-arm/ebldr_stage0.elf

Exit status: 0 when the image fits with at least --min-free bytes
spare, 1 when it fits the region but is under the minimum (or does not
fit at all), 2 on a usage or tool error.
"""

import argparse
import subprocess
import sys

DEFAULT_BUDGET = 16384
DEFAULT_MIN_FREE = 16


def parse_size_output(text):
    """Parse `size` Berkeley output; return (text, data) in bytes.

    Expected shape (one data line; extra files and a total line are
    ignored by taking the first data line)::

        text    data     bss     dec     hex filename
       16232       4     388   16624    40f0 ebldr_stage0.elf
    """
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 6 and fields[0].isdigit() and fields[1].isdigit():
            return int(fields[0]), int(fields[1])
    raise ValueError("no size data line in output: %r" % text.strip())


def check(used, budget, min_free):
    """Return (free, ok, message) for a measured flash use."""
    free = budget - used
    if used > budget:
        return free, False, (
            "stage-0 uses %d bytes of its %d-byte region "
            "(overflowed by %d)" % (used, budget, -free))
    if free < min_free:
        return free, False, (
            "stage-0 has %d bytes free in its %d-byte region, below the "
            "%d-byte minimum" % (free, budget, min_free))
    return free, True, (
        "stage-0 flash: %d / %d bytes (%d free; minimum free %d)"
        % (used, budget, free, min_free))


def top_symbols(elf, nm, count):
    """Largest symbols in the image, as (size, name) pairs.

    Best effort only: an empty list when nm is missing or fails, so a
    missing listing never fails the gate.
    """
    if count <= 0:
        return []
    try:
        result = subprocess.run(
            [nm, "--print-size", "--size-sort", "--radix=d", elf],
            capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return []
    if result.returncode != 0:
        return []
    symbols = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[1].isdigit():
            symbols.append((int(fields[1]), fields[3]))
    return sorted(symbols, reverse=True)[:count]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--elf", required=True,
                        help="stage-0 ELF to measure")
    parser.add_argument("--size", default="arm-none-eabi-size",
                        help="size tool (default: %(default)s)")
    parser.add_argument("--nm", default="arm-none-eabi-nm",
                        help="nm tool for the top-symbols listing "
                        "(default: %(default)s)")
    parser.add_argument("--budget", type=int, default=DEFAULT_BUDGET,
                        help="flash region size in bytes (default: "
                        "%(default)s)")
    parser.add_argument("--min-free", type=int, default=DEFAULT_MIN_FREE,
                        help="minimum free bytes (default: %(default)s)")
    parser.add_argument("--top", type=int, default=10,
                        help="largest symbols to list, 0 to skip "
                        "(default: %(default)s)")
    args = parser.parse_args(argv)

    try:
        result = subprocess.run([args.size, args.elf],
                                capture_output=True, text=True, timeout=60)
    except OSError as error:
        print("::error::cannot run %s: %s" % (args.size, error),
              file=sys.stderr)
        return 2
    if result.returncode != 0:
        print("::error::%s failed for %s: %s"
              % (args.size, args.elf, result.stderr.strip()), file=sys.stderr)
        return 2
    try:
        text, data = parse_size_output(result.stdout)
    except ValueError as error:
        print("::error::%s" % error, file=sys.stderr)
        return 2

    free, ok, message = check(text + data, args.budget, args.min_free)
    print(message)
    if not ok:
        print("::error::%s -- a change that adds code stage-0 links "
              "needs a slimmer verify path or a larger stage-0; see #207"
              % message, file=sys.stderr)
        return 1
    for size, name in top_symbols(args.elf, args.nm, args.top):
        print("  %6d  %s" % (size, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
