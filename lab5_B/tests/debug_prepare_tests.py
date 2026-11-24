#!/usr/bin/env python3
from pathlib import Path
import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('debug_prepare',ROOT/'scripts/prepare_debug.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def sample_elf():
    data=bytearray(64+64*3)
    data[:6]=b'\x7fELF\x02\x01'
    struct.pack_into('<Q',data,24,0x800020)
    struct.pack_into('<Q',data,40,64)
    struct.pack_into('<HHH',data,58,64,3,2)
    names=b'\0.text\0.shstrtab\0'
    struct.pack_into('<IIQQQQIIQQ',data,128,1,1,6,0x800000,0,32,0,0,16,0)
    struct.pack_into('<IIQQQQIIQQ',data,192,7,3,0,0,len(data),len(names),0,0,1,0)
    return bytes(data)+names

class DebugPrepareTests(unittest.TestCase):
    def test_reads_text_instead_of_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'user.out';p.write_bytes(sample_elf())
            self.assertEqual(module.elf_addresses(p),(0x800020,0x800000))
    def test_rejects_non_elf64(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'bad.out';p.write_bytes(b'not an ELF')
            with self.assertRaises(ValueError):module.elf_addresses(p)
    def test_local_source_candidates_are_version_local(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);directory=root/'target/riscv';directory.mkdir(parents=True)
            (directory/'example.c').write_text('static bool trans_ecall(int x) {}\ntarget_ulong HELPER(sret)(void *e) {}\n')
            found=module.candidates(root)
            self.assertEqual(found['trans_ecall'][0]['line'],1)
            self.assertEqual(found['helper_sret'][0]['line'],2)
            self.assertFalse(found['trans_sret'])
    def test_guest_script_uses_native_commands_and_paths_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix='lab5 debug ') as tmp:
            project=Path(tmp)/'project with spaces'
            (project/'obj').mkdir(parents=True)
            (project/'bin').mkdir()
            (project/'obj/__user_hello.out').write_bytes(sample_elf())
            (project/'bin/kernel').write_bytes(b'kernel placeholder')
            (project/'bin/ucore.img').write_bytes(b'image placeholder')
            subprocess.run([sys.executable,str(ROOT/'scripts/prepare_debug.py'),
                            '--project',str(project)],check=True,capture_output=True,text=True)
            generated=list((project/'B_debug').glob('*/guest.gdb'))
            self.assertEqual(len(generated),1)
            script=generated[0].read_text()
            self.assertNotIn('gdb_helpers',script)
            self.assertNotIn('lab5-ecall',script)
            self.assertIn('add-symbol-file "'+str(project/'obj/__user_hello.out')+'" 0x800000',script)
            self.assertIn('set logging file '+str(generated[0].parent/'guest.log')+'\n',script)
            self.assertIn('set logging enabled on\ntarget remote localhost:1234',script)
            self.assertIn('hbreak user/libs/syscall.c:sys_putc',script)

if __name__=='__main__':unittest.main(verbosity=2)
