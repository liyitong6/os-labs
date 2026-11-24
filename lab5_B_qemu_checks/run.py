#!/usr/bin/env python3
"""Build and run a separate real-B RISC-V fixture; do not modify Lab5."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent
EXPECTED=['S_IRQ_STACK','CONTEXT_SWITCH','USER_ECALL_ABI','COW_SHARED_WRITE',
          'COW_LAST_REFERENCE','COW_NESTED_KERNEL_STORE','COW_PREPARED_KERNEL_STORE',
          'TIMER_PREEMPT_AND_KILL','READONLY_REJECT','KERNEL_ACCESS_REJECT','COW_OOM_PRESERVE']

def tool(name):
    result=shutil.which(name)
    if result is None:raise ValueError('Required command not found: '+name)
    return result

def build_plan(project,package,output,cc,objcopy):
    installer_path=package/'scripts/install_b.py'
    spec=importlib.util.spec_from_file_location('lab5_b_installer',installer_path)
    installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
    proc=(project/'kern/process/proc.c').read_text()
    pieces=['#include <proc.h>\n#include <sync.h>\n#include <pmm.h>\n#include <sched.h>\n#include <assert.h>\n#include <error.h>\n#include <riscv.h>\nvoid switch_to(struct context *,struct context *);\n']
    for name in installer.PROC_NAMES:
        span=installer.function_span(proc,name)
        if span is None:raise ValueError('B function is not installed: '+name)
        pieces.append(proc[span[0]:span[1]]+'\n')
    (output/'proc_b.c').write_text('\n'.join(pieces))
    common=['-march=rv64imac_zicsr_zifencei','-mabi=lp64','-mcmodel=medany',
            '-msmall-data-limit=0','-mno-relax','-O2','-g','-std=gnu99','-ffreestanding',
            '-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie',
            '-ffunction-sections','-fdata-sections','-nostdinc','-Wall','-Wno-unused']
    uinc=['-I'+str(ROOT/'include'),'-I'+str(project/'user/libs'),'-I'+str(project/'libs')]
    user_sources=[ROOT/'user.S',ROOT/'user.c',project/'user/libs/syscall.c']
    commands=[];user_objects=[]
    for i,p in enumerate(user_sources):
        obj=output/f'user_{i}.o';user_objects.append(obj)
        commands.append([cc,*common,*uinc,'-c',str(p),'-o',str(obj)])
    commands.append([cc,*common,'-nostdlib','-nostartfiles','-Wl,--gc-sections,--no-relax',
                     '-T',str(ROOT/'user.ld'),*map(str,user_objects),'-lgcc','-o',str(output/'user.elf')])
    commands.append([objcopy,'-O','binary',str(output/'user.elf'),str(output/'user.bin')])
    image=''' .section .user_image,"a"
.balign 4096
.globl fixture_user_image_start
fixture_user_image_start:
.incbin USER_BINARY
.globl fixture_user_image_end
fixture_user_image_end:
.balign 4096
'''.replace('USER_BINARY',json.dumps(str(output/'user.bin'),ensure_ascii=False))
    (output/'image.S').write_text(image)
    dirs=['libs','kern/debug','kern/driver','kern/trap','kern/mm','kern/libs',
          'kern/sync','kern/process','kern/schedule','kern/syscall']
    kinc=['-I'+str(ROOT/'include'),*['-I'+str(project/d) for d in dirs]]
    kernel_sources=[ROOT/'entry.S',ROOT/'kernel.c',output/'image.S',output/'proc_b.c',
                    project/'kern/trap/trap.c',project/'kern/trap/trapentry.S',
                    project/'kern/syscall/syscall.c',project/'kern/schedule/sched.c',
                    project/'kern/process/switch.S',project/'kern/mm/cow.c',
                    project/'libs/string.c',project/'libs/printfmt.c']
    objects=[]
    for i,p in enumerate(kernel_sources):
        obj=output/f'kernel_{i}.o';objects.append(obj)
        commands.append([cc,*common,*kinc,'-c',str(p),'-o',str(obj)])
    commands.append([cc,*common,'-nostdlib','-nostartfiles','-Wl,--gc-sections,--no-relax',
                     '-T',str(ROOT/'kernel.ld'),*map(str,objects),'-lgcc','-o',str(output/'kernel.elf')])
    return commands

def write_debug_scripts(output,package):
    user=output/'user.elf';kernel=output/'kernel.elf'
    data=user.read_bytes()
    if data[:6]!=b'\x7fELF\x02\x01':raise ValueError('User fixture is not ELF64 little-endian')
    entry=struct.unpack_from('<Q',data,24)[0]
    if entry!=0x800000:raise ValueError('Unexpected user entry: '+hex(entry))
    q=lambda p:json.dumps(str(p),ensure_ascii=False)
    script='\n'.join(['set pagination off','set confirm off','set architecture riscv:rv64',
                      'file '+q(kernel),'add-symbol-file '+q(user)+' 0x800000',
                      'set logging file '+str(output/'guest.log'),'set logging overwrite on','set logging enabled on',
                      'target remote localhost:1234','hbreak user/libs/syscall.c:sys_putc',
                      'echo Ready: continue to sys_putc, then x/12i $pc and thbreak at the actual ECALL address.\\n',''])
    (output/'guest.gdb').write_text(script)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project',type=Path,default=Path.home()/'oslab/lab5_B_work')
    ap.add_argument('--package',type=Path,default=ROOT.parent/'lab5_B')
    ap.add_argument('--cc',default='riscv64-unknown-elf-gcc')
    ap.add_argument('--objcopy',default='riscv64-unknown-elf-objcopy')
    ap.add_argument('--qemu',default='qemu-system-riscv64')
    ap.add_argument('--timeout',type=int,default=45)
    ap.add_argument('--build-only',action='store_true')
    ap.add_argument('--debug',action='store_true',help='Run with -s -S for a separate guest GDB')
    args=ap.parse_args()
    if args.timeout<1 or args.timeout>60:ap.error('Timeout must be between 1 and 60 seconds')
    project=args.project.expanduser().resolve();package=args.package.expanduser().resolve()
    for p in [project/'kern/mm/cow.c',project/'kern/process/proc.c',package/'scripts/install_b.py']:
        if not p.is_file():ap.error('Missing prerequisite: '+str(p))
    output=ROOT/'build';output.mkdir(exist_ok=True)
    manifest={}
    sources=[project/'kern/trap/trap.c',project/'kern/trap/trapentry.S',project/'kern/syscall/syscall.c',
             project/'kern/schedule/sched.c',project/'kern/process/switch.S',project/'kern/mm/cow.c',
             project/'kern/mm/cow.h',project/'kern/process/proc.c',project/'user/libs/syscall.c']
    for p in sources:manifest[str(p.relative_to(project))]=hashlib.sha256(p.read_bytes()).hexdigest()
    print('Building independent fixture from your installed B sources...',flush=True)
    commands=build_plan(project,package,output,tool(args.cc),tool(args.objcopy))
    log=[]
    for cmd in commands:
        result=subprocess.run(cmd,capture_output=True,text=True)
        log.append('COMMAND '+json.dumps(cmd)+'\n'+result.stdout+result.stderr)
        if result.returncode:
            (output/'build.log').write_text('\n'.join(log))
            print(result.stdout+result.stderr)
            print('BUILD FAILED. Full log:',output/'build.log')
            return result.returncode
    (output/'build.log').write_text('\n'.join(log))
    write_debug_scripts(output,package)
    print('Fixture build PASS:',output/'kernel.elf',flush=True)
    status={'scope':'B runtime only; A/C backends and page allocation are fixture substitutes',
            'project':str(project),'b_source_sha256':manifest,'target_build':'passed','qemu_runtime':'not run'}
    (output/'status.json').write_text(json.dumps(status,indent=2)+'\n')
    if args.build_only:return 0
    qemu=tool(args.qemu)
    version=subprocess.run([qemu,'--version'],capture_output=True,text=True,check=True).stdout.splitlines()[0]
    status['qemu_version']=version
    cmd=[qemu,'-machine','virt','-cpu','rv64','-smp','1','-m','128M','-nographic',
         '-monitor','none','-bios','default','-kernel',str(output/'kernel.elf'),'-no-reboot']
    if args.debug:
        cmd.extend(['-s','-S'])
        print('Debug server starts paused. In another Ubuntu 22.04 terminal execute:',flush=True)
        print('riscv64-unknown-elf-gdb -x '+str(output/'guest.gdb'),flush=True)
        print('Press Ctrl+C to stop this manual debug run.',flush=True)
        return subprocess.call(cmd)
    print('Running '+version+'; timeout '+str(args.timeout)+' seconds...',flush=True)
    started=time.monotonic()
    process=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    timed_out=False
    try:content,_=process.communicate(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        timed_out=True;process.kill();content,_=process.communicate()
    (output/'qemu.log').write_text(content)
    print(content,end='')
    missing=[name for name in EXPECTED if '[B-QEMU] PASS '+name+'\n' not in content]
    success=(not timed_out and process.returncode==0 and not missing and
             'B_QEMU_RESULT: PASS' in content and 'B_QEMU_RESULT: FAIL' not in content)
    status.update(qemu_runtime='passed' if success else 'failed',elapsed_seconds=round(time.monotonic()-started,3),
                  returncode=process.returncode,timed_out=timed_out,missing_markers=missing)
    (output/'status.json').write_text(json.dumps(status,indent=2)+'\n')
    print('\nRuntime log:',output/'qemu.log')
    if success:
        print('PASS: all 11 B runtime checkpoints. Not a full Lab5 grade.')
        return 0
    print('FAILED or TIMED OUT. Missing markers:',', '.join(missing) or 'none')
    return 1

if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as e:
        print('ERROR:',e,file=sys.stderr);sys.exit(1)
