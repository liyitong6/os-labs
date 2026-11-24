#!/usr/bin/env python3
from pathlib import Path
import importlib.util
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('installer',ROOT/'scripts/install_b.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=Path(self.tmp.name)
        with zipfile.ZipFile(ROOT/'base/lab5.zip') as z:z.extractall(self.root)
        self.project=self.root/'lab5_new'
    def tearDown(self):self.tmp.cleanup()
    def test_preview_is_read_only(self):
        old={str(p.relative_to(self.project)):p.read_bytes() for p in self.project.rglob('*') if p.is_file()}
        installer.make_plan(self.project,'2412933')
        self.assertEqual(old,{str(p.relative_to(self.project)):p.read_bytes() for p in self.project.rglob('*') if p.is_file()})
    def test_apply_and_idempotence(self):
        plan=installer.make_plan(self.project,'2412933')
        self.assertIsNotNone(installer.apply_plan(self.project,plan))
        self.assertIsNone(installer.apply_plan(self.project,installer.make_plan(self.project,'2412933')))
        proc=(self.project/'kern/process/proc.c').read_text()
        for name in installer.PROC_NAMES:self.assertIsNotNone(installer.function_span(proc,name))
    def test_ac_regions_and_grading_preserved(self):
        proc=self.project/'kern/process/proc.c'
        ac='\n/* A/C sample with } and { in strings/comments */\nint do_exit(int e) { const char *s="}"; /* { */ return e; }\nint do_fork(int x) { return x+123; }\n'
        proc.write_text(proc.read_text()+ac)
        official=(self.project/'tools/grade.sh').read_bytes()
        mm=(self.project/'kern/mm/pmm.c').read_bytes()
        installer.apply_plan(self.project,installer.make_plan(self.project,'2412933'))
        self.assertTrue(proc.read_text().endswith(ac))
        self.assertEqual(official,(self.project/'tools/grade.sh').read_bytes())
        self.assertEqual(mm,(self.project/'kern/mm/pmm.c').read_bytes())
    def test_existing_b_functions_replace_without_duplicates(self):
        proc=self.project/'kern/process/proc.c'
        proc.write_text(proc.read_text()+'\nvoid cpu_idle(void) { for (;;) {} }\nint do_yield(void) { return 999; }\n')
        plan=installer.make_plan(self.project,'2412933')
        installer.apply_plan(self.project,plan)
        text=proc.read_text();self.assertNotIn('return 999;',text)
        for name in installer.PROC_NAMES:self.assertIsNotNone(installer.function_span(text,name))
    def test_custom_file_conflict_changes_nothing(self):
        target=self.project/'kern/trap/trap.c'
        target.write_text(target.read_text()+'\n/* custom work */\n')
        before=target.read_bytes()
        with self.assertRaisesRegex(ValueError,'custom changes'):installer.make_plan(self.project,'2412933')
        self.assertEqual(before,target.read_bytes())
        self.assertFalse((self.project/'kern/mm/cow.c').exists())
    def test_student_id_is_scoped_to_b(self):
        target=self.project/'kern/process/proc.c'
        target.write_text(target.read_text()+'\n/* A/C keeps literal 2412933 */\n')
        installer.apply_plan(self.project,installer.make_plan(self.project,'2468101'))
        text=target.read_text()
        self.assertIn('/* A/C keeps literal 2412933 */',text)
        self.assertIn('//lab5 function4 2468101',text)
        self.assertIn('lab5 function2 YOUR CODE',text)
    def test_backup_matches_previous_bytes(self):
        previous=(self.project/'kern/trap/trap.c').read_bytes()
        backup=installer.apply_plan(self.project,installer.make_plan(self.project,'2412933'))
        self.assertEqual(previous,(backup/'kern/trap/trap.c').read_bytes())

if __name__=='__main__':unittest.main(verbosity=2)
