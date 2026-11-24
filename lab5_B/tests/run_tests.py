#!/usr/bin/env python3
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--sanitize',action='store_true')
    args=ap.parse_args()
    cc=shutil.which('gcc') or shutil.which('cc')
    if cc is None:ap.exit(2,'Host C compiler not found\n')
    out=ROOT/'tests/build';out.mkdir(exist_ok=True)
    binary=out/('host_tests_sanitize' if args.sanitize else 'host_tests')
    cmd=[cc,'-std=gnu99','-O1' if args.sanitize else '-O2','-g','-Wall','-Wextra',
         '-Wno-unused-parameter','-Wno-misleading-indentation',
         '-I'+str(ROOT/'tests/include'),'-I'+str(ROOT/'overlay/kern/mm'),
         str(ROOT/'tests/host_tests.c'),'-o',str(binary)]
    if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True)
    env=os.environ.copy()
    if args.sanitize:env['ASAN_OPTIONS']='detect_leaks=0'
    subprocess.run([str(binary)],check=True,env=env)
    subprocess.run([sys.executable,str(ROOT/'tests/assembly_semantics.py')],check=True)
    subprocess.run([sys.executable,str(ROOT/'tests/installer_tests.py')],check=True)
    subprocess.run([sys.executable,str(ROOT/'tests/debug_prepare_tests.py')],check=True)

if __name__=='__main__':
    try:main()
    except subprocess.CalledProcessError as e:sys.exit(e.returncode)
