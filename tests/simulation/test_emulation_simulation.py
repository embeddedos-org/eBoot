import unittest

class TesteBootSimulation(unittest.TestCase):
    def test_nor_flash_memory_mapping(self):
        # Simulate memory-mapped SPI NOR Flash
        FLASH_BASE = 0x08000000
        FIRMWARE_OFFSET = 0x00010000
        target_addr = FLASH_BASE + FIRMWARE_OFFSET
        assert target_addr == 0x08010000, "NOR flash memory mapping incorrect"
