#include <riscv.h>
.section .text.entry,"ax"
.balign 4
.globl _start
_start:
    bnez a0, .Lpark
    csrw sie, zero
    csrw sscratch, zero
    la t0, __bss_start
    la t1, __bss_end
.Lclear:
    bgeu t0, t1, .Lready
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear
.Lready:
    la sp, fixture_boot_stack_top
    call fixture_main
.Lpark:
    wfi
    j .Lpark

.text
.balign 4
.globl fixture_kernel_irq_probe
fixture_kernel_irq_probe:
    addi sp, sp, -32
    sd ra, 0(sp)
    sd s2, 8(sp)
    li s2, 0x11223344
    li t0, 0x55667788
    li t1, 2
    csrs sip, t1
.Lwait_soft:
    csrr t1, sip
    andi t1, t1, 2
    bnez t1, .Lwait_soft
    li t1, 0x11223344
    bne s2, t1, .Lirq_bad
    li t1, 0x55667788
    bne t0, t1, .Lirq_bad
    li a0, 1
    j .Lirq_done
.Lirq_bad:
    li a0, 0
.Lirq_done:
    ld s2, 8(sp)
    ld ra, 0(sp)
    addi sp, sp, 32
    ret

.section .bss,"aw",@nobits
.balign 16
.globl fixture_boot_stack
fixture_boot_stack:
    .space 16384
.globl fixture_boot_stack_top
fixture_boot_stack_top:
