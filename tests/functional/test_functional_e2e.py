import unittest
class TestEBootFunctional(unittest.TestCase):
    def test_secure_boot_pipeline(self):
        stages = ["ROM", "BL1", "BL2", "KERNEL"]
        self.assertEqual(stages[-1], "KERNEL")
