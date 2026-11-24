#!/usr/bin/env python3
"""Interpret the instructions in B's actual assembly; not an assembler/QEMU run."""
from pathlib import Path
import random
import re

ROOT=Path(__file__).resolve().parents[1]
MASK=(1<<64)-1
NAMES=['zero','ra','sp','gp','tp','t0','t1','t2','s0','s1',
       'a0','a1','a2','a3','a4','a5','a6','a7','s2','s3','s4','s5','s6','s7',
       's8','s9','s10','s11','t3','t4','t5','t6']
REG={name:i for i,name in enumerate(NAMES)}|{f'x{i}':i for i in range(32)}
CSR={'0x143':'stval'}

def program(text):
    code=[];labels={}
    for line in text.splitlines():
        line=line.split('#',1)[0].strip()
        if not line or line.startswith(('.', 'LOCAL', '//')):continue
        if line.endswith(':'):labels[line[:-1]]=len(code);continue
        parts=line.split(None,1)
        args=[a.strip() for a in parts[1].split(',')] if len(parts)>1 else []
        code.append((parts[0],args))
    return code,labels

class Machine:
    def __init__(self,seed):
        rng=random.Random(seed)
        self.r=[0]+[rng.getrandbits(64) for _ in range(31)]
        self.c={'sscratch':0,'sstatus':0,'sepc':0x800020,'stval':0x2468,'scause':8}
        self.mem={};self.pc=None;self.mode='S';self.interrupt_seen=False
    def read(self,name):return self.r[REG[name]]
    def write(self,name,value):
        if REG[name]:self.r[REG[name]]=value&MASK
    def imm(self,s):
        s=s.replace('REGBYTES','8').replace('SSTATUS_SPP','256').replace(' ','')
        if not re.fullmatch(r'-?(?:0x[0-9a-fA-F]+|[0-9]+)(?:\*(?:0x[0-9a-fA-F]+|[0-9]+))*',s):
            raise AssertionError('unsupported immediate '+s)
        v=1
        for part in s.split('*'):v*=int(part,0)
        return v
    def address(self,s):
        m=re.fullmatch(r'(.+)\((\w+)\)',s)
        return (self.read(m[2])+self.imm(m[1]))&MASK
    def run(self,text):
        code,labels=program(text);i=0;steps=0
        while i<len(code):
            steps+=1;assert steps<500
            op,a=code[i];i+=1
            if op=='csrrw':
                key=CSR.get(a[1],a[1]);old=self.c[key];self.c[key]=self.read(a[2]);self.write(a[0],old)
            elif op=='csrr':self.write(a[0],self.c[CSR.get(a[1],a[1])])
            elif op=='csrw':self.c[CSR.get(a[0],a[0])]=self.read(a[1])
            elif op=='addi':self.write(a[0],self.read(a[1])+self.imm(a[2]))
            elif op=='andi':self.write(a[0],self.read(a[1])&self.imm(a[2]))
            elif op in ('STORE','sd'):self.mem[self.address(a[1])]=self.read(a[0])
            elif op in ('LOAD','ld'):self.write(a[0],self.mem[self.address(a[1])])
            elif op=='bnez':
                if self.read(a[0]):i=labels[a[1]]
            elif op=='j':i=labels[a[0]]
            elif op=='ret':self.pc=self.read('ra');return
            else:raise AssertionError('unsupported instruction '+op)
            if self.c['sstatus']&2:self.interrupt_seen=True
    def sret(self):
        s=self.c['sstatus'];self.mode='S' if s&256 else 'U';self.pc=self.c['sepc']
        self.c['sstatus']=(s&~(2|256)) | (2 if s&32 else 0) | 32

def macro(text,name):
    return re.search(r'\.macro\s+'+name+r'\b(.*?)\.endm',text,re.S)[1]

def trap_roundtrips():
    text=(ROOT/'overlay/kern/trap/trapentry.S').read_text()
    save=macro(text,'SAVE_ALL');restore=macro(text,'RESTORE_ALL');count=0
    for seed in range(50):
        for user in [False,True]:
            for enabled in [False,True]:
                for nested in [False,True]:
                    m=Machine(seed);ktop=0xffffffffc0400000
                    m.write('sp',0x70001000 if user else ktop-128)
                    original=list(m.r);epc=0x800020+seed*4
                    m.c.update(sscratch=ktop if user else 0,sepc=epc,
                               sstatus=(0 if user else 256)|(32 if enabled else 0)|0x40000)
                    expected_top=ktop if user else original[2]
                    m.run(save);frame=m.read('sp')
                    assert frame==expected_top-288 and m.c['sscratch']==0
                    assert [m.mem[frame+8*i] for i in range(32)]==original
                    assert m.mem[frame+33*8]==epc
                    if nested:
                        intermediate=list(m.r)
                        m.c.update(sstatus=256|0x40000,sepc=0xffffffffc0201234)
                        m.run(save);m.run(restore);m.sret()
                        assert m.r==intermediate and m.c['sscratch']==0 and m.mode=='S'
                    m.interrupt_seen=False
                    m.run(restore)
                    assert not m.interrupt_seen
                    m.sret()
                    assert m.r==original and m.pc==epc
                    assert m.mode==('U' if user else 'S')
                    assert bool(m.c['sstatus']&2)==enabled
                    assert m.c['sscratch']==(ktop if user else 0)
                    count+=1
    return count

def switch_roundtrips():
    text=(ROOT/'overlay/kern/process/switch.S').read_text().split('switch_to:',1)[1]
    selected=['ra','sp']+[f's{i}' for i in range(12)]
    for seed in range(100):
        m=Machine(seed);rng=random.Random(seed+1000)
        m.write('a0',0x1000);m.write('a1',0x2000)
        original=[m.read(r) for r in selected]
        target=[rng.getrandbits(64) for _ in selected]
        for i,value in enumerate(target):m.mem[0x2000+8*i]=value
        m.run(text)
        assert [m.mem[0x1000+8*i] for i in range(14)]==original
        assert [m.read(r) for r in selected]==target and m.pc==target[0]
    return 100

def main():
    traps=trap_roundtrips();switches=switch_roundtrips()
    print(f'PASS {traps} actual-assembly U/S, nested-trap and interrupt-state semantic scenarios')
    print(f'PASS {switches} actual-assembly context save/restore scenarios (14 x 8 bytes)')
    print('Scope: source-instruction semantic model; not native RISC-V execution.')

if __name__=='__main__':main()
