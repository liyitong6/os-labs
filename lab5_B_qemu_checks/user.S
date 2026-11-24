#include <fixture_shared.h>
.section .text.entry,"ax"
.globl fixture_user_entry
fixture_user_entry:
    call fixture_user_main
    li a0, 1
    li a1, -99
    ecall
1:  j 1b

.text
.globl fixture_user_abi_probe
fixture_user_abi_probe:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    li ra, 0x111
    li gp, 0x222
    li s2, 0x333
    li s3, 0x444
    li t0, 0x501
    li t1, 0x502
    li t2, 0x503
    li t3, 0x504
    li t4, 0x505
    li t5, 0x506
    li t6, 0x507
    li a1, 0x601
    li a2, 0x602
    li a3, 0x603
    li a4, 0x604
    li a5, 0x605
    li a6, 0x606
    li a7, 0x607
    li a0, 18
    ecall
    addi a0, a0, -FX_PID
    bnez a0, .Labi_bad
    li a0, 0x111
    bne ra, a0, .Labi_bad
    li a0, 0x222
    bne gp, a0, .Labi_bad
    li a0, 0x333
    bne s2, a0, .Labi_bad
    li a0, 0x444
    bne s3, a0, .Labi_bad
    li a0, 0x501
    bne t0, a0, .Labi_bad
    li a0, 0x502
    bne t1, a0, .Labi_bad
    li a0, 0x503
    bne t2, a0, .Labi_bad
    li a0, 0x504
    bne t3, a0, .Labi_bad
    li a0, 0x505
    bne t4, a0, .Labi_bad
    li a0, 0x506
    bne t5, a0, .Labi_bad
    li a0, 0x507
    bne t6, a0, .Labi_bad
    li a0, 0x601
    bne a1, a0, .Labi_bad
    li a0, 0x602
    bne a2, a0, .Labi_bad
    li a0, 0x603
    bne a3, a0, .Labi_bad
    li a0, 0x604
    bne a4, a0, .Labi_bad
    li a0, 0x605
    bne a5, a0, .Labi_bad
    li a0, 0x606
    bne a6, a0, .Labi_bad
    li a0, 0x607
    bne a7, a0, .Labi_bad
    li a0, 1
    j .Labi_done
.Labi_bad:
    li a0, 0
.Labi_done:
    ld s3, 24(sp)
    ld s2, 16(sp)
    ld gp, 8(sp)
    ld ra, 0(sp)
    addi sp, sp, 48
    ret
