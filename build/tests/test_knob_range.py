"""Checks for the knob range law patched into LineSel-cloned edit handlers."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linker

BLOB = (linker.REPEAT98_BUILD / "linesel_handlers.bin").read_bytes()


class KnobRange(unittest.TestCase):
    def test_max_150_or_less_keeps_linesel_law(self):
        plain = linker._patch_linesel_knob_clone(BLOB, 5, 32)
        for m in (1, 6, 100, 150):
            self.assertEqual(linker._patch_linesel_knob_clone(BLOB, 5, 32, m), plain)

    def test_max_200_matches_apandly(self):
        out = linker._patch_linesel_knob_clone(BLOB, 5, 32, 200)
        # MVK.S1 201,A6 / MVK.S1 128,A4 / SHL.S1 A4,23,A4 (128 << 23 = 2.0f)
        self.assertEqual(out[0x72:0x74], bytes.fromhex("522f"))
        self.assertEqual(out[0x76:0x78], bytes.fromhex("1206"))
        self.assertEqual(out[0x84:0x86], bytes.fromhex("02f6"))
        diff = [i for i in range(len(BLOB)) if out[i] != BLOB[i]]
        self.assertTrue(all(0x70 <= i < 0x86 or 0xA4 <= i < 0xA6 for i in diff), diff)

    def test_unknown_max_rejected(self):
        with self.assertRaises(ValueError):
            linker._patch_linesel_knob_clone(BLOB, 5, 32, 250)


if __name__ == "__main__":
    unittest.main()
