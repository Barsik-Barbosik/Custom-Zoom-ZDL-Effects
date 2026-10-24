"""Regression checks for calls in dynamically loaded init code."""
import struct
import sys
import unittest
from pathlib import Path

# Ported from themanro's build/tests/.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linker


class InitCalls(unittest.TestCase):
    def test_calls_follow_text_rebase(self):
        init_va = 0x7A0
        targets = [0x380, 0x3CC, 0x1800]
        body = linker._init_materialize_body(targets, init_va)
        for i, target in enumerate(targets):
            off = len(linker._INIT_MAT_PROLOGUE) + i * linker._INIT_MAT_STRIDE
            off += linker._INIT_MAT_BRANCH_OFF
            word = struct.unpack_from('<I', body, off)[0]
            self.assertEqual(word & ~(0x1FFFFF << 7), 0x12)  # direct B.S2
            delta = (word >> 7) & 0x1FFFFF
            if delta & (1 << 20):
                delta -= 1 << 21
            for load_base in [0, 0x10000000, 0x11820000]:
                pc = (load_base + init_va + off) & ~31
                self.assertEqual(pc + delta * 4, load_base + target)

    def test_zero_call_probe_keeps_frame(self):
        self.assertEqual(linker._init_materialize_body([], 0x7A0),
                         linker._INIT_MAT_PROLOGUE + linker._INIT_MAT_EPILOGUE)

    def test_setup_block_precedes_calls(self):
        # ctx[34] block (init_param_setup) sits between prologue and calls;
        # the call branches still follow a rebase after the shift.
        init_va = 0x7A0 & ~31
        targets = [0x380, 0x1800]
        body = linker._init_materialize_body(targets, init_va, 56)
        p = len(linker._INIT_MAT_PROLOGUE)
        self.assertEqual(body[:p], linker._INIT_MAT_PROLOGUE)
        self.assertEqual(body[p:p + len(linker._INIT_SETUP)], linker._INIT_SETUP)
        self.assertEqual(len(linker._INIT_SETUP) % 32, 0)
        for i, target in enumerate(targets):
            off = p + len(linker._INIT_SETUP) + i * linker._INIT_MAT_STRIDE
            off += linker._INIT_MAT_BRANCH_OFF
            word = struct.unpack_from('<I', body, off)[0]
            delta = (word >> 7) & 0x1FFFFF
            if delta & (1 << 20):
                delta -= 1 << 21
            for load_base in [0, 0x11820000]:
                pc = (load_base + init_va + off) & ~31
                self.assertEqual(pc + delta * 4, load_base + target)
        self.assertTrue(body.endswith(linker._INIT_MAT_EPILOGUE))

    def test_setup_byte_count_patch(self):
        for n in (28, 40, 56):
            blk = linker._init_setup_block(n)
            word = struct.unpack_from('<I', blk, linker._INIT_SETUP_COUNT_OFF)[0]
            self.assertEqual((word >> 7) & 0xFFFF, n)
            # only the count field differs from the template
            tmpl = struct.unpack_from('<I', linker._INIT_SETUP, linker._INIT_SETUP_COUNT_OFF)[0]
            self.assertEqual(word & ~(0xFFFF << 7), tmpl & ~(0xFFFF << 7))
            self.assertEqual(blk[:0x54] + blk[0x58:],
                             linker._INIT_SETUP[:0x54] + linker._INIT_SETUP[0x58:])
        for bad in (0, 3, 60):
            with self.assertRaises(ValueError):
                linker._init_setup_block(bad)

    def test_rejects_invalid_branch_layout(self):
        for targets, base in [([0x380], 1), ([0x381], 0x7A0), ([0x800000], 0x7A0)]:
            with self.assertRaises(ValueError):
                linker._init_materialize_body(targets, base)


if __name__ == '__main__':
    unittest.main()
