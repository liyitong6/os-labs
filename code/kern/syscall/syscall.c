#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <vmm.h>
#include <error.h>
#include <assert.h>

//lab5 function1 2412933
static int sys_exit(uint64_t arg[]) {
    return do_exit((int)arg[0]);
}

static int sys_fork(uint64_t arg[]) {
    return do_fork(0, current->tf->gpr.sp, current->tf);
}

/* Reject wrapping ranges before the framework's VMA permission check. */
static bool syscall_user_range(uintptr_t addr, size_t len, bool write) {
    return current->mm != NULL && len != 0 &&
           USERBASE <= addr && addr < USERTOP && len <= USERTOP - addr &&
           user_mem_check(current->mm, addr, len, write);
}

static int sys_wait(uint64_t arg[]) {
    int *store = (int *)(uintptr_t)arg[1];
    if (store != NULL && !syscall_user_range((uintptr_t)store, sizeof(*store), 1)) {
        return -E_INVAL;
    }
    /* A must use the agreed COW-aware user write path when storing the code. */
    return do_wait((int)arg[0], store);
}

static int sys_exec(uint64_t arg[]) {
    const char *name = (const char *)(uintptr_t)arg[0];
    size_t len = (size_t)arg[1];
    unsigned char *binary = (unsigned char *)(uintptr_t)arg[2];
    size_t size = (size_t)arg[3];
    if (!syscall_user_range((uintptr_t)name, len, 0) ||
        !syscall_user_range((uintptr_t)binary, size, 0)) {
        return -E_INVAL;
    }
    /* do_execve must stage user-backed inputs before releasing the old mm. */
    return do_execve(name, len, binary, size);
}

static int sys_yield(uint64_t arg[]) {
    return do_yield();
}

static int sys_kill(uint64_t arg[]) {
    return do_kill((int)arg[0]);
}

static int sys_getpid(uint64_t arg[]) {
    return current->pid;
}

static int sys_putc(uint64_t arg[]) {
    cputchar((int)arg[0]);
    return 0;
}

static int sys_pgdir(uint64_t arg[]) {
    print_pgdir();
    return 0;
}

static int (*const syscalls[])(uint64_t arg[]) = {
    [SYS_exit] = sys_exit,
    [SYS_fork] = sys_fork,
    [SYS_wait] = sys_wait,
    [SYS_exec] = sys_exec,
    [SYS_yield] = sys_yield,
    [SYS_kill] = sys_kill,
    [SYS_getpid] = sys_getpid,
    [SYS_putc] = sys_putc,
    [SYS_pgdir] = sys_pgdir,
};

#define NUM_SYSCALLS (sizeof(syscalls) / sizeof(syscalls[0]))

void syscall(void) {
    assert(current != NULL && current->tf != NULL);
    struct trapframe *tf = current->tf;
    uint64_t number = tf->gpr.a0;
    int result = -E_INVAL;
    if (number < NUM_SYSCALLS && syscalls[number] != NULL) {
        uint64_t arg[5] = {
            tf->gpr.a1, tf->gpr.a2, tf->gpr.a3, tf->gpr.a4, tf->gpr.a5
        };
        result = syscalls[number](arg);
    }
    tf->gpr.a0 = (uintptr_t)(intptr_t)result;
}
