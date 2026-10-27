#!/usr/bin/env python3
"""Check module A in a temporary copy, without requiring B/C's handlers.

Uses the supplied context/breakpoint test, with a test-only breakpoint handler.
This is not the final Lab3 grade and never changes the original source tree.
"""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import tempfile


TEST_HANDLERS = r'''
/* Test fixture only: not the submitted implementations of modules B/C. */
void interrupt_handler(struct trapframe *tf) {
    print_trapframe(tf);
    panic("Unexpected interrupt in module A test");
}

void exception_handler(struct trapframe *tf) {
    if (tf->cause != CAUSE_BREAKPOINT) {
        print_trapframe(tf);
        panic("Unexpected exception in module A test");
    }
    cprintf("Breakpoint at 0x%016lx (A test fixture)\n", tf->epc);
    uint16_t instruction = *(const uint16_t *)tf->epc;
    tf->epc += ((instruction & 3) == 3) ? 4 : 2;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-riscv64"))
    parser.add_argument("--gccprefix", default="riscv64-unknown-elf-")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if not shutil.which(args.gccprefix + "gcc") or not shutil.which(args.qemu):
        raise SystemExit("Missing RISC-V compiler or QEMU; check the selected paths.")

    with tempfile.TemporaryDirectory(prefix="lab3-module-a-") as scratch:
        work = Path(scratch) / "code"
        shutil.copytree(root, work, ignore=shutil.ignore_patterns("obj", "bin", "__pycache__", ".git"))
        trap = work / "kern/trap/trap.c"
        source = trap.read_text()
        marker = "//lab3 function2 YOUR CODE"
        if source.count(marker) != 1 or "void exception_handler(" in source:
            raise SystemExit("This A-only checker expects the untouched module B/C placeholder.")
        trap.write_text(source.replace(marker, marker + "\n" + TEST_HANDLERS))
        subprocess.run([sys.executable, str(work / "tools/inject.py"), str(work)], check=True)

        # The supplied probe deliberately sets s0-s11. Preserve its C caller's
        # callee-saved values in a separate test shim, leaving the probe unchanged.
        saved = ["ra", "s0", "s1"] + ["s" + str(i) for i in range(2, 12)]
        shim = [".text", ".align 2", ".globl module_a_call_context", "module_a_call_context:", "    addi sp, sp, -112"]
        shim += [f"    sd {reg}, {8 * i}(sp)" for i, reg in enumerate(saved)]
        shim += ["    call test_lab3_register_context"]
        shim += [f"    ld {reg}, {8 * i}(sp)" for i, reg in enumerate(saved)]
        shim += ["    addi sp, sp, 112", "    ret", ""]
        (work / "kern/test/module_a_context_shim.S").write_text("\n".join(shim))
        test_c = work / "kern/test/test_lab3.c"
        source = test_c.read_text().replace("#include <stdio.h>", "#include <stdio.h>\n#include <sbi.h>")
        source = source.replace("extern uintptr_t test_lab3_register_context(void);", "extern uintptr_t module_a_call_context(void);")
        source = source.replace("test_lab3_register_context()", "module_a_call_context()")
        passed = '    cprintf("[Test 1] Trap entry and breakpoint return: PASS (35/35)\\n");'
        if source.count(passed) != 1:
            raise SystemExit("Supplied test output anchor changed; no test was run.")
        source = source.replace(passed, passed + "\n    sbi_shutdown();\n    while (1) {}")
        test_c.write_text(source)

        subprocess.run(["make", "--no-print-directory", "DEFS=-DGRADE_LAB3_CONTEXT", "GCCPREFIX=" + args.gccprefix], cwd=work, check=True)
        command = [args.qemu, "-machine", "virt", "-nographic", "-bios", "default", "-kernel", "bin/ucore.img", "-monitor", "none", "-no-reboot"]
        try:
            result = subprocess.run(command, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=12)
            output = result.stdout.decode(errors="replace")
        except subprocess.TimeoutExpired as exc:
            print((exc.stdout or b"").decode(errors="replace"))
            raise SystemExit("Module A check timed out; save this output for diagnosis.")
        print(output)
        checks = ["check_alloc_page() succeeded!", "Breakpoint at", "[Test 1] Trap entry and breakpoint return: PASS"]
        if result.returncode != 0 or any(item not in output for item in checks):
            raise SystemExit("MODULE A CHECK: FAIL")
        print("MODULE A CHECK: PASS")
        print("Scope: PMM startup + supplied register-context/breakpoint-return test.")
        print("Final Lab3 grading still requires the real interrupt/exception handlers.")


if __name__ == "__main__":
    main()
