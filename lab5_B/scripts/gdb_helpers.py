"""Optional helpers for a GDB with Python support. Not loaded by generated scripts.

The default workflow uses native GDB commands; see docs/GDB.md.
"""
import gdb

class Lab5ECALL(gdb.Command):
    def __init__(self):super().__init__('lab5-ecall',gdb.COMMAND_USER)
    def invoke(self,arg,from_tty):
        frame=gdb.selected_frame();pc=int(gdb.parse_and_eval('$pc'))
        for insn in frame.architecture().disassemble(pc,count=80):
            if insn['asm'].strip().split()[0]=='ecall':
                address=insn['addr']
                gdb.set_convenience_variable('lab5_return_pc',gdb.Value(address+4))
                gdb.write('ECALL at %#x; expected ordinary return %#x\n'%(address,address+4))
                if address!=pc:
                    gdb.Breakpoint('*%#x'%address,type=gdb.BP_HARDWARE_BREAKPOINT,temporary=True)
                    gdb.execute('continue')
                return
        raise gdb.GdbError('No ECALL in this linear instruction range; stop in user sys_putc/getpid first.')

class Lab5SRET(gdb.Command):
    def __init__(self):super().__init__('lab5-sret',gdb.COMMAND_USER)
    def invoke(self,arg,from_tty):
        if gdb.convenience_variable('lab5_return_pc') is None:
            raise gdb.GdbError('Run lab5-ecall before lab5-sret.')
        bp=gdb.Breakpoint('__lab5_sret',type=gdb.BP_HARDWARE_BREAKPOINT,temporary=True)
        bp.condition='$sepc == $lab5_return_pc'
        gdb.execute('continue')

class Lab5Registers(gdb.Command):
    def __init__(self):super().__init__('lab5-regs',gdb.COMMAND_USER)
    def invoke(self,arg,from_tty):
        for name in ['pc','sp','ra','a0','a1','a2','a3','a4','a5','sstatus','sepc','scause','stval','sscratch','priv']:
            try:gdb.write('%-9s %s\n'%(name,gdb.parse_and_eval('$'+name)))
            except gdb.error:gdb.write('%-9s unavailable in this target XML\n'%name)

Lab5ECALL();Lab5SRET();Lab5Registers()
