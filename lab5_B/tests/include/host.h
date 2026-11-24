#ifndef LAB5_B_HOST_H
#define LAB5_B_HOST_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>

typedef int64_t bool;
#define __noreturn __attribute__((noreturn))
#define ROUNDDOWN(a,n) ((a) - (a) % (n))
#define PGSIZE 4096
#define RISCV_PGSHIFT 12
#define SATP_MODE_SV39 8
#define USERBASE UINT64_C(0x200000)
#define USERTOP UINT64_C(0x80000000)
#define PTE_V 0x001
#define PTE_R 0x002
#define PTE_W 0x004
#define PTE_X 0x008
#define PTE_U 0x010
#define PTE_G 0x020
#define PTE_A 0x040
#define PTE_D 0x080
#define VM_READ 1
#define VM_WRITE 2
#define SSTATUS_SIE 2
#define SSTATUS_SPIE 0x20
#define SSTATUS_SPP 0x100
#define SSTATUS_SUM 0x40000
#define IRQ_S_TIMER 5
#define IRQ_S_SOFT 1
#define MIP_SSIP 2
#define CAUSE_USER_ECALL 8
#define CAUSE_STORE_PAGE_FAULT 15
#define CAUSE_LOAD_PAGE_FAULT 13
#define CAUSE_ILLEGAL_INSTRUCTION 2
#define E_INVAL 3
#define E_NO_MEM 4
#define E_BAD_PROC 2
#define E_KILLED 9
#define SYS_exit 1
#define SYS_fork 2
#define SYS_wait 3
#define SYS_exec 4
#define SYS_yield 10
#define SYS_kill 12
#define SYS_getpid 18
#define SYS_putc 30
#define SYS_pgdir 31
#define PF_EXITING 1
#define WT_INTERRUPTED UINT32_C(0x80000000)
#define WT_CHILD (WT_INTERRUPTED | 1)

typedef struct list_entry { struct list_entry *prev, *next; } list_entry_t;
static inline void list_init(list_entry_t *h) { h->next=h->prev=h; }
static inline list_entry_t *list_next(list_entry_t *h) { return h->next; }
static inline void list_add_tail(list_entry_t *h,list_entry_t *n) {
    n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n;
}
struct context { uintptr_t ra,sp,s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11; };
struct pushregs {
    uintptr_t zero,ra,sp,gp,tp,t0,t1,t2,s0,s1,a0,a1,a2,a3,a4,a5,a6,a7;
    uintptr_t s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,t3,t4,t5,t6;
};
struct trapframe { struct pushregs gpr; uintptr_t status,epc,tval,cause; };
typedef uintptr_t pte_t;
typedef uintptr_t pde_t;
struct mm_struct { pde_t *pgdir; uintptr_t start,end; bool writable; };
enum proc_state { PROC_UNINIT,PROC_SLEEPING,PROC_RUNNABLE,PROC_ZOMBIE };
struct proc_struct {
    enum proc_state state; int pid,runs; uintptr_t kstack; volatile bool need_resched;
    struct proc_struct *parent; struct mm_struct *mm; struct context context;
    struct trapframe *tf; uintptr_t pgdir; uint32_t flags; char name[16];
    list_entry_t list_link,hash_link; int exit_code; uint32_t wait_state;
    struct proc_struct *cptr,*yptr,*optr;
};
#define le2proc(le,member) ((struct proc_struct *)((char *)(le)-offsetof(struct proc_struct,member)))
struct Page { int ref,allocated; unsigned char data[PGSIZE]; };
extern struct proc_struct *current,*idleproc,*initproc;
extern list_entry_t proc_list;
extern struct mm_struct *check_mm_struct;
extern volatile size_t ticks;
extern uintptr_t host_sstatus,host_satp,host_sip,host_sscratch,host_stvec;
#define read_csr(reg) (host_##reg)
#define write_csr(reg,val) ((void)(host_##reg=(uintptr_t)(val)))
#define set_csr(reg,val) ((void)(host_##reg|=(uintptr_t)(val)))
#define clear_csr(reg,val) ((void)(host_##reg&=~(uintptr_t)(val)))
static inline bool host_intr_save(void) {
    bool enabled=!!(host_sstatus&SSTATUS_SIE); host_sstatus&=~(uintptr_t)SSTATUS_SIE; return enabled;
}
static inline void host_intr_restore(bool f) { if(f) host_sstatus|=SSTATUS_SIE; }
#define local_intr_save(f) ((f)=host_intr_save())
#define local_intr_restore(f) host_intr_restore(f)
void host_panic(const char *,...) __noreturn;
#define panic(...) host_panic(__VA_ARGS__)
int cprintf(const char *,...);
void warn(const char *,...);
void cputchar(int);
void print_pgdir(void);
void clock_set_next_event(void);
void flush_tlb(void);
void tlb_invalidate(pde_t *,uintptr_t);
pte_t *get_pte(pde_t *,uintptr_t,bool);
struct Page *host_alloc_page(void);
void host_free_page(struct Page *);
#define alloc_page() host_alloc_page()
#define free_page(p) host_free_page(p)
struct Page *pte2page(pte_t);
void *page2kva(struct Page *);
static inline int page_ref(struct Page *p) { return p->ref; }
static inline void set_page_ref(struct Page *p,int r) { p->ref=r; }
int page_insert(pde_t *,struct Page *,uintptr_t,uint32_t);
bool user_mem_check(struct mm_struct *,uintptr_t,size_t,bool);
int do_exit(int);
int do_fork(uint32_t,uintptr_t,struct trapframe *);
int do_wait(int,int *);
int do_execve(const char *,size_t,unsigned char *,size_t);
int do_yield(void);
int do_kill(int);
struct proc_struct *find_proc(int);
void schedule(void);
void wakeup_proc(struct proc_struct *);
void proc_run(struct proc_struct *);
void switch_to(struct context *,struct context *);
void syscall(void);
void __alltraps(void);
void print_regs(struct pushregs *);
#endif
