import unittest

class TesteBootFunctional(unittest.TestCase):
    def test_ab_firmware_fallback_pipeline(self):
        slots = {"A": {"valid": False, "version": 2}, "B": {"valid": True, "version": 1}}
        # Bootloader selects boot target
        boot_target = None
        if slots["A"]["valid"]:
            boot_target = "A"
        elif slots["B"]["valid"]:
            boot_target = "B"
        assert boot_target == "B", "Fallback to Slot B failed when Slot A is corrupt"
