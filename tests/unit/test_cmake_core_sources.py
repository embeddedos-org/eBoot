"""Ensure every core implementation is built exactly once."""

import re
import unittest
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CMAKELISTS = ROOT / "CMakeLists.txt"
CORE_DIR = ROOT / "core"


def _registered_core_sources():
    text = CMAKELISTS.read_text(encoding="utf-8")
    match = re.search(r"add_library\(eboot_core STATIC(?P<body>.*?)\n\)", text, re.DOTALL)
    if match is None:
        raise AssertionError("eboot_core source list was not found in CMakeLists.txt")
    return re.findall(r"core/[A-Za-z0-9_]+\.c", match.group("body"))


class CoreSourceRegistrationTests(unittest.TestCase):
    def test_every_core_source_is_built(self):
        expected = {
            path.relative_to(ROOT).as_posix()
            for path in CORE_DIR.glob("*.c")
        }
        registered = set(_registered_core_sources())

        self.assertEqual(
            expected,
            registered,
            "eboot_core must contain every core/*.c implementation",
        )

    def test_core_sources_are_registered_once(self):
        counts = Counter(_registered_core_sources())
        duplicates = sorted(source for source, count in counts.items() if count > 1)
        self.assertFalse(duplicates, f"duplicate eboot_core sources: {duplicates}")


if __name__ == "__main__":
    unittest.main()
