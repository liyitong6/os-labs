#!/usr/bin/env python3
"""Prepare scripts for an already-built, integrated Lab5. Does not launch GDB."""
from pathlib import Path
import argparse
import datetime
import json
import re
import shlex
import shutil
import struct
import subprocess

PACKAGE=Path(__file__).resolve().parents[1]

def elf_addresses(path):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x02\x01':raise ValueError('Expected a little-endian ELF64: '+str(path))
    entry=struct.unpack_from('<Q',data,24)[0]
    offset=struct.unpack_from('<Q',data,40)[0]
    size,count,names_index=struct.unpack_from('<HHH',data,58)
    if size<64 or not count or names_index>=count:raise ValueError('Unsupported ELF section table')
    headers=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*size) for i in range(count)]
    strings=headers[names_index]
    names=data[strings[4]:strings[4]+strings[5]]
    for h in headers:
        stop=names.find(b'\0',h[0])
        if names[h[0]:stop]==b'.text':return entry,h[3]
    raise ValueError('ELF .text section missing')

def candidates(source):
    names=['trans_ecall','trans_sret','riscv_cpu_do_interrupt','helper_sret']
    found={n:[] for n in names}
    for p in sorted((source/'target/riscv').rglob('*.c*')):
        for no,line in enumerate(p.read_text(errors='replace').splitlines(),1):
            for name in names:
                patterns=[r'\b'+name+r'\s*\(']
                if name=='helper_sret':patterns.append(r'\bHELPER\s*\(\s*sret\s*\)')
                if any(re.search(x,line) for x in patterns):
                    found[name].append({'file':str(p.relative_to(source)),'line':no,'text':line.strip()})
    return found

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--program',default='hello')
    ap.add_argument('--qemu-bin',type=Path)
    ap.add_argument('--qemu-source',type=Path)
    args=ap.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_]+',args.program):ap.error('Invalid program name')
    project=args.project.expanduser().resolve()
    user=project/('obj/__user_'+args.program+'.out')
    kernel=project/'bin/kernel';image=project/'bin/ucore.img'
    for path in [user,kernel,image]:
        if not path.is_file():ap.error('Build the integrated project first; missing '+str(path))
    entry,text=elf_addresses(user)
    stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    out=project/'B_debug'/stamp;out.mkdir(parents=True)
    quote=lambda x:json.dumps(str(x))
    guest='\n'.join([
        'set pagination off','set confirm off','set architecture riscv:rv64',
        'file '+quote(kernel),'add-symbol-file '+quote(user)+' '+hex(text),
        'set logging file '+str(out/'guest.log'),'set logging overwrite on','set logging enabled on',
        'target remote localhost:1234','hbreak user/libs/syscall.c:sys_putc',
        'echo Ready: continue to sys_putc, then x/12i $pc and thbreak at the actual ECALL address.\\n',''])
    (out/'guest.gdb').write_text(guest)
    meta={'program':args.program,'entry':hex(entry),'text':hex(text),
          'status':'scripts prepared; no debugger session executed'}
    qemu=args.qemu_bin.expanduser().resolve() if args.qemu_bin else None
    if qemu:
        if not qemu.is_file():ap.error('QEMU binary missing')
        version=subprocess.run([str(qemu),'--version'],capture_output=True,text=True,check=True)
        meta['qemu_version']=version.stdout.splitlines()[0]
        qargs=['-machine','virt','-nographic','-bios','default',
               '-device','loader,file='+str(image)+',addr=0x80200000','-s','-S']
        host='\n'.join([
            'set pagination off','set confirm off','set breakpoint pending on',
            'file '+quote(qemu),'set args '+shlex.join(qargs),
            'set logging file '+str(out/'host.log'),'set logging overwrite on','set logging enabled on',
            'break riscv_cpu_do_interrupt','break helper_sret','disable',
            'echo Runtime breakpoints start disabled. Enable the needed one at the guest ECALL/SRET stop.\\n',
            'run',''])
        (out/'host.gdb').write_text(host)
    if args.qemu_source:
        source=args.qemu_source.expanduser().resolve()
        if not (source/'target/riscv').is_dir():ap.error('QEMU target/riscv source missing')
        (out/'source_candidates.json').write_text(json.dumps(candidates(source),ensure_ascii=False,indent=2)+'\n')
        if (source/'VERSION').is_file():meta['source_version']=(source/'VERSION').read_text().strip()
        if shutil.which('git'):
            commit=subprocess.run(['git','-C',str(source),'rev-parse','HEAD'],capture_output=True,text=True)
            if commit.returncode==0:meta['source_commit']=commit.stdout.strip()
    (out/'session.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    print('Prepared:',out)
    print('Guest script:',out/'guest.gdb')
    if qemu:print('Host script:',out/'host.gdb')
    print('Source matches are search candidates; verify definitions and binary/source identity locally.')

if __name__=='__main__':main()
