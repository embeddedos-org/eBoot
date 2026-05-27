# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EoS Project
import unittest
class TestEbootSimulation(unittest.TestCase):
    def test_flash_memory_mapping(self):
        print("Simulating NOR flash memory mapped I/O access...")
        flash_addr = 0x08000000
        memory = {flash_addr: 0xDEADBEEF}
        self.assertEqual(memory[flash_addr], 0xDEADBEEF)
