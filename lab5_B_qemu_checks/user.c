#include <defs.h>
#include <syscall.h>
#include <error.h>
#include <fixture_shared.h>

int fixture_user_abi_probe(void);

static void fail(int number) {
    sys_exit(-1000-number);
    for (;;) asm volatile("nop");
}

#define CHECK(expr,n) do { if (!(expr)) fail(n); } while (0)

static int invalid_syscall(void) {
    register uintptr_t a0 asm("a0")=0;
    asm volatile("ecall" : "+r"(a0) : : "memory");
    return (int)(intptr_t)a0;
}

void fixture_user_main(uintptr_t phase) {
    if (phase==1) {
        *(volatile uint64_t *)FX_READONLY=123;
        fail(91);
    }
    if (phase==2) {
        volatile uint64_t value=*(volatile uint64_t *)0x80200000;
        (void)value;
        fail(92);
    }
    if (phase==3) {
        *(volatile uint64_t *)FX_OOM=456;
        fail(93);
    }
    CHECK(phase==0,1);
    CHECK(sys_getpid()==FX_PID,2);
    CHECK(fixture_user_abi_probe()==1,3);
    CHECK(invalid_syscall()==-E_INVAL,4);
    CHECK(sys_putc('U')==0,5);
    CHECK(sys_fork()==77,6);
    CHECK(*(volatile uint64_t *)FX_DATA==0x1111,7);
    *(volatile uint64_t *)FX_DATA=0x2222;
    CHECK(*(volatile uint64_t *)FX_DATA==0x2222,8);
    *(volatile uint64_t *)FX_LAST=0x4444;
    CHECK(*(volatile uint64_t *)FX_LAST==0x4444,9);
    /* Fixture wait 77 deliberately writes through COW to exercise an S-mode
     * fault nested inside the real user syscall. Wait 78 prepares it first.
     */
    CHECK(sys_wait(77,(int *)FX_NESTED)==0,10);
    CHECK(*(volatile int *)FX_NESTED==FX_WAIT_MAGIC,11);
    CHECK(sys_wait(78,(int *)FX_PREPARED)==0,12);
    CHECK(*(volatile int *)FX_PREPARED==FX_PREPARE_MAGIC,13);
    CHECK(sys_wait(77,(int *)0x80200000)==-E_INVAL,14);
    CHECK(sys_wait(77,(int *)FX_READONLY)==-E_INVAL,15);
    CHECK(sys_exec("fixture",7,(unsigned char *)FX_USER_CODE,32)==-E_UNIMP,16);
    CHECK(sys_exec((const char *)0x80200000,4,(unsigned char *)FX_USER_CODE,32)==-E_INVAL,17);
    CHECK(sys_pgdir()==0,18);
    CHECK(sys_yield()==0,19);
    CHECK(sys_kill(3)==0,20);
    CHECK(sys_kill(1)==-E_INVAL,21);
    /* No voluntary yield here: the timer must preempt this running user loop. */
    for (volatile uintptr_t i=0;i<20000000;i++) asm volatile("nop");
    CHECK(sys_getpid()==FX_PID,22);
    sys_exit(0);
    fail(23);
}
