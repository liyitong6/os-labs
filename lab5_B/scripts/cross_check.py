#!/usr/bin/env python3
"""Compile B-owned translation units after applying the overlay to Lab5."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--cc',default='riscv64-unknown-elf-gcc')
    args=ap.parse_args()
    compiler=shutil.which(args.cc)
    if compiler is None:
        ap.exit(2,'RISC-V compiler unavailable: '+args.cc+'\n')
    root=args.project.resolve()
    out=root/'B_build_check';out.mkdir(exist_ok=True)
    flags=['-mcmodel=medany','-O2','-std=gnu99','-fno-builtin','-Wall','-Wno-unused',
           '-nostdinc','-fno-stack-protector','-ffunction-sections','-fdata-sections','-g']
    includes=['libs','kern/debug','kern/driver','kern/trap','kern/mm','kern/libs',
              'kern/sync','kern/process','kern/schedule','kern/syscall']
    for name in ['kern/syscall/syscall.c','kern/trap/trap.c','kern/schedule/sched.c',
                 'kern/process/proc.c','kern/mm/cow.c','kern/trap/trapentry.S','kern/process/switch.S']:
        cmd=[compiler,*flags,*['-I'+str(root/p) for p in includes],'-c',str(root/name),
             '-o',str(out/(name.replace('/','_')+'.o'))]
        subprocess.run(cmd,check=True)
        print('PASS',name)
    name='user/libs/syscall.c'
    subprocess.run([compiler,*flags,'-I'+str(root/'user/libs'),'-I'+str(root/'libs'),
                    '-c',str(root/name),'-o',str(out/'user_syscall.o')],check=True)
    print('PASS',name)
    print('B translation units compiled; this is not a full link or runtime test.')

if __name__=='__main__':
    try: main()
    except subprocess.CalledProcessError as e: sys.exit(e.returncode)
