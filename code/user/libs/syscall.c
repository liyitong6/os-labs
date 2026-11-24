#include <defs.h>
#include <unistd.h>
#include <syscall.h>

//lab5 function1 user ABI fix 2412933
/* Fixed-width, fixed-arity inputs avoid reading nonexistent variadic arguments.
 * The result stays in a register: no 64-bit store into a 32-bit int object.
 */
static inline int syscall(uintptr_t number, uintptr_t p0, uintptr_t p1,
                          uintptr_t p2, uintptr_t p3, uintptr_t p4)
{
    register uintptr_t a0 asm("a0") = number;
    register uintptr_t a1 asm("a1") = p0;
    register uintptr_t a2 asm("a2") = p1;
    register uintptr_t a3 asm("a3") = p2;
    register uintptr_t a4 asm("a4") = p3;
    register uintptr_t a5 asm("a5") = p4;
    asm volatile ("ecall"
                  : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3), "+r"(a4), "+r"(a5)
                  :
                  : "memory");
    return (int)(intptr_t)a0;
}

int sys_exit(int64_t code) {
    return syscall(SYS_exit, (uintptr_t)code, 0, 0, 0, 0);
}

int sys_fork(void) {
    return syscall(SYS_fork, 0, 0, 0, 0, 0);
}

int sys_wait(int64_t pid, int *store) {
    return syscall(SYS_wait, (uintptr_t)pid, (uintptr_t)store, 0, 0, 0);
}

int sys_exec(const char *name, size_t len, unsigned char *binary, size_t size) {
    return syscall(SYS_exec, (uintptr_t)name, len, (uintptr_t)binary, size, 0);
}

int sys_yield(void) {
    return syscall(SYS_yield, 0, 0, 0, 0, 0);
}

int sys_kill(int64_t pid) {
    return syscall(SYS_kill, (uintptr_t)pid, 0, 0, 0, 0);
}

int sys_getpid(void) {
    return syscall(SYS_getpid, 0, 0, 0, 0, 0);
}

int sys_putc(int64_t c) {
    return syscall(SYS_putc, (uintptr_t)c, 0, 0, 0, 0);
}

int sys_pgdir(void) {
    return syscall(SYS_pgdir, 0, 0, 0, 0, 0);
}
