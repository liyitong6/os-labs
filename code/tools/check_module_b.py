#!/usr/bin/env python3
"""Isolated Lab4 B runtime check; never supplies production A/C implementations."""
from pathlib import Path
import hashlib, os, shlex, shutil, subprocess, sys, tempfile

FIXTURE = {'harness.c': '#include <defs.h>\n#include <proc.h>\n#include <sched.h>\n#include <trap.h>\n#include <riscv.h>\n#include <intr.h>\n#include <clock.h>\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n#include <sbi.h>\n\nextern void b_context_entry(void);\nextern int b_yield_probe(uintptr_t seed);\nextern void forkrets(struct trapframe *) __attribute__((noreturn));\n\nuintptr_t b_expected[14], b_snapshot[14];\nstruct context *b_last_from;\nvolatile unsigned b_switches;\nstatic uintptr_t roots[3][512] __attribute__((aligned(4096)));\nstatic uintptr_t middle[3][512] __attribute__((aligned(4096)));\nstatic uintptr_t leaf[3][512] __attribute__((aligned(4096)));\nstatic uintptr_t data[3][512] __attribute__((aligned(4096)));\nstatic unsigned char stacks[3][8192] __attribute__((aligned(16)));\nstatic struct proc_struct idle, workers[3], sleeper, zombie;\nstatic unsigned starts, trace[8], trace_count;\n#define MAGIC ((uintptr_t)0x2412933000000000ULL)\n\nvoid cons_putc(int c) { sbi_console_putchar(c); }\n\nvoid __attribute__((noreturn))\n__panic(const char *file, int line, const char *fmt, ...)\n{\n    va_list ap;\n    cprintf("\\nMODULE B RUNTIME CHECK: FAIL at %s:%d\\n", file, line);\n    va_start(ap, fmt);\n    vcprintf(fmt, ap);\n    va_end(ap);\n    cprintf("\\n");\n    sbi_shutdown();\n    for (;;) asm volatile("wfi");\n}\n\nstatic void check_saved(void)\n{\n    uintptr_t actual[14];\n    assert(b_last_from != NULL);\n    memcpy(actual, b_last_from, sizeof(actual));\n    for (unsigned i = 0; i < 14; i++) assert(actual[i] == b_expected[i]);\n}\n\nstatic void check_space(void)\n{\n    assert(read_csr(satp) == (((uintptr_t)8 << 60) | (current->pgdir >> 12)));\n    unsigned slot = 0;\n    while (slot < 3 && current->pgdir != (uintptr_t)roots[slot]) slot++;\n    assert(slot < 3);\n    assert(*(volatile uintptr_t *)0x40000000UL == (MAGIC | slot));\n}\n\nvoid __attribute__((noreturn)) b_first(void)\n{\n    uintptr_t expected[14];\n    assert(!(read_csr(sstatus) & SSTATUS_SIE));\n    memcpy(expected, &current->context, sizeof(expected));\n    for (unsigned i = 0; i < 14; i++) assert(b_snapshot[i] == expected[i]);\n    check_saved();\n    starts++;\n    forkrets(current->tf);\n}\n\nstatic void __attribute__((noreturn)) worker(void)\n{\n    assert(read_csr(sstatus) & SSTATUS_SIE);\n    check_space();\n    if (current->pid == 5) {\n        assert(starts == 3 && trace_count == 8);\n        check_saved();\n        cprintf("[B cpu_idle dispatch] PASS\\n");\n        cprintf("MODULE B RUNTIME CHECK: PASS\\n");\n        sbi_shutdown();\n        for (;;) asm volatile("wfi");\n    }\n    for (unsigned round = 0; round < 4; round++) {\n        check_saved();\n        check_space();\n        assert(trace_count < 8);\n        trace[trace_count++] = current->pid;\n        bool enabled = (current->pid + round) & 1;\n        if (enabled) intr_enable(); else intr_disable();\n        current->need_resched = 1;\n        if (round == 3) current->state = PROC_SLEEPING;\n        assert(b_yield_probe((uintptr_t)current->pid * 4096 + round * 32) == 1);\n        assert(round < 3);\n        assert(current->need_resched == 0);\n        assert(!!(read_csr(sstatus) & SSTATUS_SIE) == enabled);\n        check_saved();\n        check_space();\n    }\n    panic("test worker unexpectedly returned");\n}\n\nstatic void setup_worker(unsigned index, int pid, unsigned slot)\n{\n    struct proc_struct *p = &workers[index];\n    memset(p, 0, sizeof(*p));\n    p->pid = pid;\n    p->state = PROC_RUNNABLE;\n    p->parent = idleproc;\n    p->pgdir = (uintptr_t)roots[slot];\n    p->kstack = (uintptr_t)stacks[index];\n    p->tf = (struct trapframe *)(stacks[index] + sizeof(stacks[index]) - sizeof(*p->tf));\n    memset(p->tf, 0, sizeof(*p->tf));\n    p->tf->gpr.sp = (uintptr_t)p->tf;\n    uintptr_t gp, tp;\n    asm volatile("mv %0, gp" : "=r"(gp));\n    asm volatile("mv %0, tp" : "=r"(tp));\n    p->tf->gpr.gp = gp;\n    p->tf->gpr.tp = tp;\n    p->tf->status = SSTATUS_SPP | SSTATUS_SPIE;\n    p->tf->epc = (uintptr_t)worker;\n    uintptr_t context[14];\n    context[0] = (uintptr_t)b_context_entry;\n    context[1] = (uintptr_t)p->tf;\n    for (unsigned i = 2; i < 14; i++) context[i] = pid * 4096 + i;\n    memcpy(&p->context, context, sizeof(context));\n}\n\nvoid __attribute__((noreturn)) b_main(void)\n{\n    intr_disable();\n    write_csr(sie, 0);\n    idt_init();\n    for (unsigned i = 0; i < 3; i++) {\n        roots[i][2] = ((uintptr_t)0x80000 << 10) | 0xcf;\n        roots[i][1] = (((uintptr_t)middle[i] >> 12) << 10) | 1;\n        middle[i][0] = (((uintptr_t)leaf[i] >> 12) << 10) | 1;\n        leaf[i][0] = (((uintptr_t)data[i] >> 12) << 10) | 0xc7;\n        data[i][0] = MAGIC | i;\n    }\n    idleproc = current = &idle;\n    idle.state = PROC_RUNNABLE;\n    idle.pgdir = (uintptr_t)roots[0];\n    list_init(&proc_list);\n    write_csr(satp, ((uintptr_t)8 << 60) | (idle.pgdir >> 12));\n    asm volatile("sfence.vma" ::: "memory");\n    check_space();\n    cprintf("B isolated runtime fixture: manual PCBs; production B functions\\n");\n\n    for (unsigned enabled = 0; enabled < 2; enabled++) {\n        if (enabled) intr_enable(); else intr_disable();\n        current->need_resched = 1;\n        schedule();\n        proc_run(current);\n        assert(current == idleproc && current->need_resched == 0);\n        assert(idle.runs == (int)enabled + 1 && b_switches == 0);\n        assert(!!(read_csr(sstatus) & SSTATUS_SIE) == enabled);\n    }\n    cprintf("[B idle fallback and same-thread] PASS\\n");\n    setup_worker(0, 1, 1);\n    setup_worker(1, 2, 2);\n    sleeper.state = PROC_SLEEPING;\n    zombie.state = PROC_ZOMBIE;\n    list_add_before(&proc_list, &workers[0].list_link);\n    list_add_before(&proc_list, &sleeper.list_link);\n    list_add_before(&proc_list, &workers[1].list_link);\n    list_add_before(&proc_list, &zombie.list_link);\n    current->need_resched = 1;\n    assert(b_yield_probe(0x7000) == 1);\n    assert(current == idleproc && starts == 2 && trace_count == 8);\n    for (unsigned i = 0; i < 8; i++) assert(trace[i] == i % 2 + 1);\n    assert(workers[0].runs == 4 && workers[1].runs == 4);\n    assert(sleeper.runs == 0 && zombie.runs == 0);\n    assert(idle.runs == 3 && current->need_resched == 0);\n    assert(read_csr(sstatus) & SSTATUS_SIE);\n    check_saved();\n    check_space();\n    cprintf("[B round-robin and blocked-state skip] PASS\\n");\n    cprintf("[B context registers and separate stacks] PASS\\n");\n    cprintf("[B Sv39 address-space switch] PASS\\n");\n    cprintf("[B interrupt-state restoration] PASS\\n");\n    cprintf("[B forkrets and first entry] PASS\\n");\n\n    asm volatile(".option push\\n.option norvc\\nebreak\\n.option pop" ::: "memory");\n    asm volatile(".option push\\n.option rvc\\nc.ebreak\\n.option pop" ::: "memory");\n    asm volatile(".option push\\n.option norvc\\n.4byte 0x0000000b\\n.option pop" ::: "memory");\n    asm volatile(".2byte 0x0000" ::: "memory");\n    cprintf("[B 16/32-bit trap return] PASS\\n");\n\n    intr_disable();\n    clock_init();\n    uintptr_t start = read_csr(time);\n    intr_enable();\n    while (ticks < 3 && read_csr(time) - start < 10000000UL) asm volatile("nop");\n    intr_disable();\n    sbi_set_timer(~(uint64_t)0);\n    clear_csr(sie, MIP_STIP);\n    assert(ticks >= 3);\n    intr_enable();\n    cprintf("[B real timer interrupts] PASS\\n");\n\n    setup_worker(2, 5, 1);\n    list_add_before(&proc_list, &workers[2].list_link);\n    current->need_resched = 1;\n    cpu_idle();\n}\n', 'boot.S': '.section .text.entry,"ax"\n.globl _start\n_start:\n    csrci sstatus, 2\n    csrw sie, zero\n    csrw satp, zero\n    sfence.vma\n    .option push\n    .option norelax\n    la gp, __global_pointer$\n    .option pop\n    la t0, __bss_start\n    la t1, __bss_end\n1:\n    bgeu t0, t1, 2f\n    sd zero, 0(t0)\n    addi t0, t0, 8\n    j 1b\n2:\n    la sp, b_stack_top\n    call b_main\n3:\n    wfi\n    j 3b\n.section .bss.boot,"aw",@nobits\n.balign 16\nb_stack:\n    .space 16384\nb_stack_top:\n', 'probes.S': '#include <riscv.h>\n.text\n.balign 4\n.globl __wrap_switch_to\n__wrap_switch_to:\n    la t0, b_expected\n    STORE ra, 0*REGBYTES(t0)\n    STORE sp, 1*REGBYTES(t0)\n    STORE s0, 2*REGBYTES(t0)\n    STORE s1, 3*REGBYTES(t0)\n    STORE s2, 4*REGBYTES(t0)\n    STORE s3, 5*REGBYTES(t0)\n    STORE s4, 6*REGBYTES(t0)\n    STORE s5, 7*REGBYTES(t0)\n    STORE s6, 8*REGBYTES(t0)\n    STORE s7, 9*REGBYTES(t0)\n    STORE s8, 10*REGBYTES(t0)\n    STORE s9, 11*REGBYTES(t0)\n    STORE s10, 12*REGBYTES(t0)\n    STORE s11, 13*REGBYTES(t0)\n    la t0, b_last_from\n    STORE a0, 0(t0)\n    la t0, b_switches\n    lw t1, 0(t0)\n    addi t1, t1, 1\n    sw t1, 0(t0)\n    tail __real_switch_to\n.balign 4\n.globl b_context_entry\nb_context_entry:\n    la t0, b_snapshot\n    STORE ra, 0*REGBYTES(t0)\n    STORE sp, 1*REGBYTES(t0)\n    STORE s0, 2*REGBYTES(t0)\n    STORE s1, 3*REGBYTES(t0)\n    STORE s2, 4*REGBYTES(t0)\n    STORE s3, 5*REGBYTES(t0)\n    STORE s4, 6*REGBYTES(t0)\n    STORE s5, 7*REGBYTES(t0)\n    STORE s6, 8*REGBYTES(t0)\n    STORE s7, 9*REGBYTES(t0)\n    STORE s8, 10*REGBYTES(t0)\n    STORE s9, 11*REGBYTES(t0)\n    STORE s10, 12*REGBYTES(t0)\n    STORE s11, 13*REGBYTES(t0)\n    tail b_first\n.balign 4\n.globl b_yield_probe\nb_yield_probe:\n    addi sp, sp, -128\n    STORE ra, 0(sp)\n    STORE a0, 112(sp)\n    STORE s0, 1*REGBYTES(sp)\n    STORE s1, 2*REGBYTES(sp)\n    STORE s2, 3*REGBYTES(sp)\n    STORE s3, 4*REGBYTES(sp)\n    STORE s4, 5*REGBYTES(sp)\n    STORE s5, 6*REGBYTES(sp)\n    STORE s6, 7*REGBYTES(sp)\n    STORE s7, 8*REGBYTES(sp)\n    STORE s8, 9*REGBYTES(sp)\n    STORE s9, 10*REGBYTES(sp)\n    STORE s10, 11*REGBYTES(sp)\n    STORE s11, 12*REGBYTES(sp)\n    addi s0, a0, 1\n    addi s1, a0, 2\n    addi s2, a0, 3\n    addi s3, a0, 4\n    addi s4, a0, 5\n    addi s5, a0, 6\n    addi s6, a0, 7\n    addi s7, a0, 8\n    addi s8, a0, 9\n    addi s9, a0, 10\n    addi s10, a0, 11\n    addi s11, a0, 12\n    call schedule\n    LOAD t0, 112(sp)\n    addi t1, t0, 1\n    bne s0, t1, .Lprobe_bad\n    addi t1, t0, 2\n    bne s1, t1, .Lprobe_bad\n    addi t1, t0, 3\n    bne s2, t1, .Lprobe_bad\n    addi t1, t0, 4\n    bne s3, t1, .Lprobe_bad\n    addi t1, t0, 5\n    bne s4, t1, .Lprobe_bad\n    addi t1, t0, 6\n    bne s5, t1, .Lprobe_bad\n    addi t1, t0, 7\n    bne s6, t1, .Lprobe_bad\n    addi t1, t0, 8\n    bne s7, t1, .Lprobe_bad\n    addi t1, t0, 9\n    bne s8, t1, .Lprobe_bad\n    addi t1, t0, 10\n    bne s9, t1, .Lprobe_bad\n    addi t1, t0, 11\n    bne s10, t1, .Lprobe_bad\n    addi t1, t0, 12\n    bne s11, t1, .Lprobe_bad\n    li a0, 1\n    j .Lprobe_restore\n.Lprobe_bad:\n    li a0, 0\n.Lprobe_restore:\n    LOAD s0, 1*REGBYTES(sp)\n    LOAD s1, 2*REGBYTES(sp)\n    LOAD s2, 3*REGBYTES(sp)\n    LOAD s3, 4*REGBYTES(sp)\n    LOAD s4, 5*REGBYTES(sp)\n    LOAD s5, 6*REGBYTES(sp)\n    LOAD s6, 7*REGBYTES(sp)\n    LOAD s7, 8*REGBYTES(sp)\n    LOAD s8, 9*REGBYTES(sp)\n    LOAD s9, 10*REGBYTES(sp)\n    LOAD s10, 11*REGBYTES(sp)\n    LOAD s11, 12*REGBYTES(sp)\n    LOAD ra, 0(sp)\n    addi sp, sp, 128\n    ret\n', 'link.ld': 'OUTPUT_ARCH(riscv)\nENTRY(_start)\nSECTIONS\n{\n    . = 0x80200000;\n    .text : { KEEP(*(.text.entry)) *(.text .text.*) }\n    . = ALIGN(4096);\n    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }\n    . = ALIGN(4096);\n    .data : { *(.data .data.* .sdata .sdata.*) }\n    PROVIDE(__global_pointer$ = . + 0x800);\n    . = ALIGN(4096);\n    __bss_start = .;\n    .bss : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) }\n    . = ALIGN(16);\n    __bss_end = .;\n    /DISCARD/ : { *(.eh_frame .note.GNU-stack) }\n}\n'}

def main():
    code = Path(__file__).resolve().parents[1]
    repo = code.parent
    materials = repo / 'report/materials/B'
    materials.mkdir(parents=True, exist_ok=True)
    def find(candidates):
        return next((str(p) for p in candidates if p and os.access(str(p), os.X_OK)), None)
    gcc = find([Path.home()/'compiler-env/riscv-elf-toolchains/bin/riscv64-unknown-elf-gcc',
                shutil.which('riscv64-unknown-elf-gcc')])
    qemu = find(['/opt/qemu/bin/qemu-system-riscv64', shutil.which('qemu-system-riscv64')])
    if not gcc or not qemu:
        raise SystemExit('Compiler or QEMU not found')
    prefix = gcc[:-3]
    ld, objcopy = prefix+'ld', prefix+'objcopy'
    if not all(os.access(p, os.X_OK) for p in (ld, objcopy)):
        raise SystemExit('Matching ld or objcopy not found')
    production = ['kern/process/proc.c', 'kern/schedule/sched.c', 'kern/process/switch.S',
                  'kern/trap/trap.c', 'kern/trap/trapentry.S', 'kern/driver/intr.c',
                  'kern/driver/clock.c', 'kern/libs/stdio.c', 'libs/printfmt.c', 'libs/string.c']
    logpath = materials/'local-b-runtime.log'
    with logpath.open('w') as log:
        def emit(text):
            print(text, flush=True)
            log.write(text+'\n')
            log.flush()
        def run(command, timeout=60):
            log.write('COMMAND: '+shlex.join(map(str, command))+'\n')
            log.flush()
            try:
                result = subprocess.run(list(map(str, command)), text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            except subprocess.TimeoutExpired as error:
                output = error.stdout or ''
                if isinstance(output, bytes): output = output.decode(errors='replace')
                if output: emit(output.rstrip())
                emit('MODULE B RUNTIME CHECK: FAIL (timeout)')
                raise SystemExit(1)
            if result.stdout: emit(result.stdout.rstrip())
            if result.returncode:
                emit('MODULE B RUNTIME CHECK: FAIL (command failed)')
                raise SystemExit(result.returncode)
            return result.stdout
        emit('Scope: production B code; manual PCBs/stacks/page tables are test fixtures.')
        emit('This is not the official full-lab 30-point grade.')
        run([gcc, '--version'])
        run([qemu, '--version'])
        for name in production:
            emit('SOURCE '+name+' '+hashlib.sha256((code/name).read_bytes()).hexdigest())
        flags = ['-march=rv64imac_zicsr_zifencei', '-mabi=lp64', '-mcmodel=medany',
                 '-mno-relax', '-msmall-data-limit=0', '-O2', '-std=gnu99', '-g',
                 '-fno-builtin', '-fno-pic', '-nostdinc', '-fno-stack-protector',
                 '-ffunction-sections', '-fdata-sections', '-Wall', '-Wno-unused',
                 '-Werror=implicit-function-declaration']
        flags += ['-I'+str(code/p) for p in ['libs', 'kern/debug', 'kern/driver',
                     'kern/trap', 'kern/mm', 'kern/sync', 'kern/process', 'kern/schedule']]
        with tempfile.TemporaryDirectory(prefix='lab4-b-runtime-') as folder:
            build = Path(folder)
            for name, content in FIXTURE.items(): (build/name).write_text(content)
            paths = [code/p for p in production]+[build/p for p in ('harness.c', 'boot.S', 'probes.S')]
            objects = []
            for i, source in enumerate(paths):
                obj = build/('b-'+str(i)+'.o')
                run([gcc, *flags, '-c', source, '-o', obj])
                objects.append(obj)
            elf, image = build/'test.elf', build/'test.bin'
            run([ld, '-m', 'elf64lriscv', '--gc-sections', '--wrap=switch_to',
                 '-T', build/'link.ld', '-o', elf, *objects])
            run([objcopy, '-O', 'binary', elf, image])
            emit('Launching QEMU for the isolated runtime checks...')
            output = run([qemu, '-machine', 'virt', '-m', '128M', '-smp', '1',
                          '-bios', 'default', '-nographic', '-monitor', 'none',
                          '-kernel', image, '-no-reboot'], timeout=30)
            required = ['[B idle fallback and same-thread] PASS',
                        '[B round-robin and blocked-state skip] PASS',
                        '[B context registers and separate stacks] PASS',
                        '[B Sv39 address-space switch] PASS',
                        '[B interrupt-state restoration] PASS',
                        '[B forkrets and first entry] PASS',
                        '[B 16/32-bit trap return] PASS',
                        '[B real timer interrupts] PASS',
                        '[B cpu_idle dispatch] PASS', 'MODULE B RUNTIME CHECK: PASS']
            if 'MODULE B RUNTIME CHECK: FAIL' in output or not all(s in output for s in required):
                emit('MODULE B RUNTIME CHECK: FAIL (required checks missing)')
                raise SystemExit(1)
        emit('B_RUNTIME_CHECK: PASS (9 checks; full-lab grade pending)')
    print('Runtime log: '+str(logpath), flush=True)

if __name__ == '__main__':
    main()
