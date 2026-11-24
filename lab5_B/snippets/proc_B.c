/* B-owned functions to merge into kern/process/proc.c, preserving A/C code. */
//lab4 dependency / lab5 B 2412933
void cpu_idle(void) {
    //lab4 dependency 2412933
    for (;;) {
        if (current->need_resched) {
            schedule();
        }
    }
}

void proc_run(struct proc_struct *proc) {
    //lab4 dependency 2412933
    assert(proc != NULL && current != NULL);
    assert(proc->state == PROC_RUNNABLE);
    if (proc != current) {
        bool intr_flag;
        local_intr_save(intr_flag);
        struct proc_struct *prev = current;
        current = proc;
        asm volatile ("" ::: "memory");
        uintptr_t target_satp = ((uintptr_t)SATP_MODE_SV39 << 60) |
                                (proc->pgdir >> RISCV_PGSHIFT);
        write_csr(satp, target_satp);
        flush_tlb();
        asm volatile ("" ::: "memory");
        switch_to(&prev->context, &proc->context);
        local_intr_restore(intr_flag);
    }
}

//lab5 function4 2412933 -- do_exit belongs to A.
int do_yield(void) {
    //lab5 function4 2412933
    assert(current != NULL);
    current->need_resched = 1;
    /* trap() schedules after the syscall; injected tests also require this. */
    return 0;
}

int do_kill(int pid) {
    //lab5 function4 2412933
    bool intr_flag;
    int result = -E_INVAL;
    local_intr_save(intr_flag);
    struct proc_struct *proc = find_proc(pid);
    if (proc != NULL && proc != idleproc && proc != initproc &&
        proc->state != PROC_UNINIT && proc->state != PROC_ZOMBIE) {
        if (proc->flags & PF_EXITING) {
            result = -E_KILLED;
        } else {
            proc->flags |= PF_EXITING;
            if (proc->state == PROC_SLEEPING &&
                (proc->wait_state & WT_INTERRUPTED)) {
                wakeup_proc(proc);
            }
            result = 0;
        }
    }
    local_intr_restore(intr_flag);
    return result;
}
