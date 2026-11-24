#!/usr/bin/env python3
"""Regress the real GDB startup failure without requiring a RISC-V debugger."""
from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fixture_runner',ROOT/'run.py')
runner=importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class FixtureDebugTests(unittest.TestCase):
    def test_native_guest_script_with_space_in_path(self):
        with tempfile.TemporaryDirectory(prefix='fixture debug ') as tmp:
            out=Path(tmp)/'build with spaces';out.mkdir()
            elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01'
            struct.pack_into('<Q',elf,24,0x800000)
            (out/'user.elf').write_bytes(elf)
            runner.write_debug_scripts(out,Path(tmp)/'package with spaces')
            script=(out/'guest.gdb').read_text()
            self.assertNotIn('source ',script)
            self.assertNotIn('lab5-ecall',script)
            self.assertIn('set logging file '+str(out/'guest.log')+'\n',script)
            self.assertIn('add-symbol-file "'+str(out/'user.elf')+'" 0x800000',script)
            self.assertIn('set logging enabled on\ntarget remote localhost:1234',script)
    def test_rejects_an_unexpected_fixture_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)
            elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01'
            struct.pack_into('<Q',elf,24,0x900000)
            (out/'user.elf').write_bytes(elf)
            with self.assertRaises(ValueError):runner.write_debug_scripts(out,out)

if __name__=='__main__':unittest.main(verbosity=2)
