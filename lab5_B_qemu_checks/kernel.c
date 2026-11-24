#include <defs.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <riscv.h>
#include <sbi.h>
#include <proc.h>
#include <trap.h>
#include <sched.h>
#include <sync.h>
#include <pmm.h>
#include <vmm.h>
#include <cow.h>
#include <error.h>
#include <fixture_shared.h>

/* Test-only platform and A/C backends. The B files are compiled unchanged. */
#define PAGE_COUNT 24
#define TABLE_COUNT 24
static unsigned char physical_pages[PAGE_COUNT][PGSIZE] __attribute__((aligned(PGSIZE)));
static struct Page descriptors[PAGE_COUNT];
static bool allocated[PAGE_COUNT];
static pte_t tables[TABLE_COUNT][512] __attribute__((aligned(PGSIZE)));
static unsigned table_count;
static pde_t *active_root,*parent_root;
static struct mm_struct user_mm;
static struct vma_struct areas[5];
static struct proc_struct processes[4];
struct proc_struct *current,*idleproc,*initproc;
list_entry_t proc_list;
struct mm_struct *check_mm_struct;
volatile size_t ticks;
static unsigned char user_kernel_stack[16384] __attribute__((aligned(16)));
static unsigned char worker_stack[16384] __attribute__((aligned(16)));
static struct Page *shared_page,*last_page,*nested_page,*prepared_page,*readonly_page,*oom_page;
static volatile unsigned nested_faults,worker_runs,fork_calls,wait_calls,exec_calls;
static unsigned phase;
static bool force_oom;
extern unsigned char fixture_user_image_start[],fixture_user_image_end[];
extern int fixture_kernel_irq_probe(void);
extern void __trapret(void);

static void stop(void) __attribute__((noreturn));
static void stop(void) {
    clear_csr(sstatus,SSTATUS_SIE);
    sbi_shutdown();
    for (;;) asm volatile("wfi");
}

void cputchar(int c) { sbi_console_putchar(c); }
static void printchar(int c,void *count) { cputchar(c);(*(int *)count)++; }
int vcprintf(const char *fmt,va_list ap) {
    int count=0;vprintfmt(printchar,&count,fmt,ap);return count;
}
int cprintf(const char *fmt,...) {
    va_list ap;va_start(ap,fmt);int n=vcprintf(fmt,ap);va_end(ap);return n;
}
void __panic(const char *file,int line,const char *fmt,...) {
    clear_csr(sstatus,SSTATUS_SIE);
    cprintf("\n[B-QEMU] FAIL %s:%d phase=%u: ",file,line,phase);
    va_list ap;va_start(ap,fmt);vcprintf(fmt,ap);va_end(ap);
    cprintf("\nB_QEMU_RESULT: FAIL\n");stop();
}
void __warn(const char *file,int line,const char *fmt,...) {
    cprintf("[B-QEMU] WARN %s:%d: ",file,line);
    va_list ap;va_start(ap,fmt);vcprintf(fmt,ap);va_end(ap);cprintf("\n");
}
static void passed(const char *name) { cprintf("[B-QEMU] PASS %s\n",name); }

void intr_enable(void) { set_csr(sstatus,SSTATUS_SIE); }
void intr_disable(void) { clear_csr(sstatus,SSTATUS_SIE); }
void clock_set_next_event(void) { sbi_set_timer(read_csr(time)+100000); }

static pde_t *new_table(void) {
    assert(table_count<TABLE_COUNT);
    pde_t *p=tables[table_count++];memset(p,0,PGSIZE);return p;
}
static pte_t table_entry(uintptr_t pa,uint32_t flags) {
    return ((pa>>PGSHIFT)<<PTE_PPN_SHIFT)|flags|PTE_V;
}
static pde_t *new_root(void) {
    pde_t *p=new_table();
    /* 1 GiB kernel identity superpage, supervisor-only. */
    p[2]=table_entry(0x80000000,PTE_R|PTE_W|PTE_X|PTE_A|PTE_D);
    return p;
}
pte_t *get_pte(pde_t *root,uintptr_t va,bool create) {
    if (!root) return NULL;
    if (read_csr(scause)==CAUSE_STORE_PAGE_FAULT &&
        (read_csr(sstatus)&SSTATUS_SPP) && va==FX_NESTED) nested_faults++;
    pde_t *table=root;
    for (int level=2;level>0;level--) {
        pte_t *e=&table[(va>>(12+level*9))&511];
        if (!(*e&PTE_V)) {
            if (!create) return NULL;
            *e=table_entry((uintptr_t)new_table(),0);
        }
        if (*e&(PTE_R|PTE_W|PTE_X)) return NULL;
        table=(pde_t *)PTE_ADDR(*e);
    }
    return &table[(va>>12)&511];
}
struct Page *alloc_pages(size_t n) {
    assert(n==1);
    if (force_oom) return NULL;
    for (unsigned i=0;i<PAGE_COUNT;i++) if (!allocated[i]) {
        allocated[i]=1;memset(&descriptors[i],0,sizeof(descriptors[i]));
        memset(physical_pages[i],0,PGSIZE);return &descriptors[i];
    }
    return NULL;
}
void free_pages(struct Page *p,size_t n) {
    assert(n==1 && p>=descriptors && p<descriptors+PAGE_COUNT && p->ref==0);
    allocated[p-descriptors]=0;
}
uintptr_t page2pa(struct Page *p) {
    assert(p>=descriptors && p<descriptors+PAGE_COUNT);
    return (uintptr_t)physical_pages[p-descriptors];
}
void *page2kva(struct Page *p) { return (void *)page2pa(p); }
struct Page *pte2page(pte_t pte) {
    uintptr_t pa=PTE_ADDR(pte),base=(uintptr_t)physical_pages;
    assert((pte&PTE_V) && base<=pa && pa<base+sizeof(physical_pages));
    assert((pa-base)%PGSIZE==0);return &descriptors[(pa-base)/PGSIZE];
}
void tlb_invalidate(pde_t *root,uintptr_t va) {
    (void)root;asm volatile("sfence.vma %0, zero"::"r"(va):"memory");
}
int page_insert(pde_t *root,struct Page *p,uintptr_t va,uint32_t perm) {
    pte_t *e=get_pte(root,va,0);
    if (!e) return -E_NO_MEM;
    p->ref++;
    if (*e&PTE_V) {
        struct Page *old=pte2page(*e);
        assert(old->ref>0);
        if (--old->ref==0) free_page(old);
    }
    *e=table_entry(page2pa(p),perm&0x3ff);
    tlb_invalidate(root,va);return 0;
}
void print_pgdir(void) { cprintf("\n[fixture] active Sv39 root=%p\n",active_root); }

bool user_mem_check(struct mm_struct *mm,uintptr_t start,size_t len,bool write) {
    if (!mm || !len || start<USERBASE || start>=USERTOP || len>USERTOP-start) return 0;
    uintptr_t end=start+len;
    while (start<end) {
        struct vma_struct *found=NULL;
        for (unsigned i=0;i<5;i++) if (areas[i].vm_start<=start && start<areas[i].vm_end) {
            found=&areas[i];break;
        }
        if (!found || !(found->vm_flags&(write?VM_WRITE:VM_READ))) return 0;
        if (write && (found->vm_flags&VM_STACK) && start<found->vm_start+PGSIZE) return 0;
        start=found->vm_end;
    }
    return 1;
}
struct proc_struct *find_proc(int pid) {
    for (unsigned i=0;i<4;i++) if (pid>0 && processes[i].pid==pid) return &processes[i];
    return NULL;
}

/* Explicit A/C substitutes: these verify B's dispatch, not fork/exec/wait. */
int do_fork(uint32_t flags,uintptr_t stack,struct trapframe *tf) {
    assert(flags==0 && tf==current->tf && !(tf->status&SSTATUS_SPP));
    assert(FX_STACK_BASE+PGSIZE<=stack && stack<=FX_STACK_TOP);
    fork_calls++;return 77;
}
int do_wait(int pid,int *store) {
    wait_calls++;
    if (pid==77) {
        assert((uintptr_t)store==FX_NESTED);
        *store=FX_WAIT_MAGIC; /* deliberately exercise a nested S-mode COW fault */
        return 0;
    }
    if (pid==78) {
        assert((uintptr_t)store==FX_PREPARED);
        int result=cow_prepare_write(current->mm,(uintptr_t)store,sizeof(*store));
        if (result) return result;
        *store=FX_PREPARE_MAGIC;return 0;
    }
    return -E_BAD_PROC;
}
int do_execve(const char *name,size_t len,unsigned char *binary,size_t size) {
    assert(len==7 && size==32 && (uintptr_t)binary==FX_USER_CODE);
    assert(memcmp(name,"fixture",7)==0);exec_calls++;return -E_UNIMP;
}

static void enter_user(unsigned next) __attribute__((noreturn));
static void enter_user(unsigned next) {
    intr_disable();
    phase=next;current=&processes[2];current->flags=0;current->state=PROC_RUNNABLE;
    current->need_resched=0;
    struct trapframe *tf=(struct trapframe *)(user_kernel_stack+sizeof(user_kernel_stack)-sizeof(*tf));
    memset(tf,0,sizeof(*tf));
    tf->gpr.sp=FX_STACK_TOP;tf->gpr.a0=next;tf->epc=FX_USER_CODE;
    tf->status=SSTATUS_SPIE|SSTATUS_SUM;
    current->tf=tf;
    asm volatile("mv sp, %0\nj __trapret"::"r"(tf):"memory");
    __builtin_unreachable();
}

int do_exit(int error) {
    intr_disable();
    if (phase==0) {
        assert(error==0);
        assert(fork_calls==1 && wait_calls==2 && exec_calls==1);
        passed("USER_ECALL_ABI");
        pte_t child=*get_pte(active_root,FX_DATA,0),parent=*get_pte(parent_root,FX_DATA,0);
        assert(pte2page(child)!=shared_page && pte2page(parent)==shared_page);
        assert(*(uint64_t *)page2kva(shared_page)==0x1111 && shared_page->ref==1);
        assert(*(uint64_t *)page2kva(pte2page(child))==0x2222);
        assert((child&PTE_W) && !(child&PTE_COW));
        for (unsigned i=8;i<PGSIZE;i++) assert(((unsigned char *)page2kva(pte2page(child)))[i]==0xa5);
        passed("COW_SHARED_WRITE");
        child=*get_pte(active_root,FX_LAST,0);
        assert(pte2page(child)==last_page && last_page->ref==1 && !(child&PTE_COW));
        assert(*(uint64_t *)page2kva(last_page)==0x4444);passed("COW_LAST_REFERENCE");
        assert(nested_faults>0 && nested_page->ref==1);
        assert(*(uint64_t *)page2kva(nested_page)==0x5555);
        child=*get_pte(active_root,FX_NESTED,0);
        assert(pte2page(child)!=nested_page && *(int *)page2kva(pte2page(child))==FX_WAIT_MAGIC);
        passed("COW_NESTED_KERNEL_STORE");
        assert(prepared_page->ref==1 && *(uint64_t *)page2kva(prepared_page)==0x6666);
        child=*get_pte(active_root,FX_PREPARED,0);
        assert(pte2page(child)!=prepared_page && *(int *)page2kva(pte2page(child))==FX_PREPARE_MAGIC);
        passed("COW_PREPARED_KERNEL_STORE");
        assert(ticks>0 && worker_runs==2 && processes[3].state==PROC_ZOMBIE);
        assert(processes[3].flags&PF_EXITING);passed("TIMER_PREEMPT_AND_KILL");
        enter_user(1);
    }
    if (phase==1) {
        assert(error==-E_KILLED && *(uint64_t *)page2kva(readonly_page)==0x7777);
        assert(!(*get_pte(active_root,FX_READONLY,0)&(PTE_W|PTE_COW)));
        passed("READONLY_REJECT");enter_user(2);
    }
    if (phase==2) {
        assert(error==-E_KILLED);passed("KERNEL_ACCESS_REJECT");
        force_oom=1;enter_user(3);
    }
    assert(phase==3 && error==-E_NO_MEM);
    assert(pte2page(*get_pte(active_root,FX_OOM,0))==oom_page && oom_page->ref==2);
    assert(*get_pte(active_root,FX_OOM,0)&PTE_COW);
    assert(*(uint64_t *)page2kva(oom_page)==0x8888);
    passed("COW_OOM_PRESERVE");
    cprintf("B_QEMU_RESULT: PASS\n");stop();
}

static void worker_entry(void) {
    worker_runs++;
    current->state=PROC_SLEEPING;current->wait_state=WT_CHILD;
    proc_run(&processes[2]);
    assert(current==&processes[3] && (current->flags&PF_EXITING));
    worker_runs++;current->state=PROC_ZOMBIE;
    schedule();panic("zombie fixture worker unexpectedly resumed");
}

static struct Page *map_data(uintptr_t va,uint64_t value,bool shared,bool writable) {
    struct Page *p=alloc_page();assert(p!=NULL);
    memset(page2kva(p),0xa5,PGSIZE);*(uint64_t *)page2kva(p)=value;
    uint32_t flags=PTE_U|PTE_R|PTE_A|(writable?PTE_COW:0);
    pte_t *e=get_pte(active_root,va,1);assert(e!=NULL);
    *e=table_entry(page2pa(p),flags);p->ref=1;
    if (shared) {
        e=get_pte(parent_root,va,1);assert(e!=NULL);
        *e=table_entry(page2pa(p),flags);p->ref++;
    }
    return p;
}

void fixture_main(void) {
    cprintf("\n[B-QEMU] isolated real-B runtime fixture\n");
    cprintf("[B-QEMU] A/C process and memory services are fixture substitutes\n");
    active_root=new_root();parent_root=new_root();
    shared_page=map_data(FX_DATA,0x1111,1,1);
    last_page=map_data(FX_LAST,0x3333,0,1);
    nested_page=map_data(FX_NESTED,0x5555,1,1);
    prepared_page=map_data(FX_PREPARED,0x6666,1,1);
    readonly_page=map_data(FX_READONLY,0x7777,0,0);
    oom_page=map_data(FX_OOM,0x8888,1,1);
    uintptr_t bytes=fixture_user_image_end-fixture_user_image_start;
    assert(bytes>0 && bytes<=4*PGSIZE && ((uintptr_t)fixture_user_image_start%PGSIZE)==0);
    for (uintptr_t i=0;i<bytes;i+=PGSIZE) {
        pte_t *e=get_pte(active_root,FX_USER_CODE+i,1);
        *e=table_entry((uintptr_t)fixture_user_image_start+i,PTE_U|PTE_R|PTE_X|PTE_A);
    }
    struct Page *stack=alloc_page();assert(stack!=NULL);stack->ref=1;
    *get_pte(active_root,FX_STACK_BASE+PGSIZE,1)=table_entry(page2pa(stack),PTE_U|PTE_R|PTE_W|PTE_A|PTE_D);
    user_mm.pgdir=active_root;
    areas[0]=(struct vma_struct){.vm_start=FX_DATA,.vm_end=FX_READONLY,.vm_flags=VM_READ|VM_WRITE};
    areas[1]=(struct vma_struct){.vm_start=FX_READONLY,.vm_end=FX_READONLY+PGSIZE,.vm_flags=VM_READ};
    areas[2]=(struct vma_struct){.vm_start=FX_USER_CODE,.vm_end=FX_USER_CODE+4*PGSIZE,.vm_flags=VM_READ|VM_EXEC};
    areas[3]=(struct vma_struct){.vm_start=FX_STACK_BASE,.vm_end=FX_STACK_TOP,.vm_flags=VM_READ|VM_WRITE|VM_STACK};
    areas[4]=(struct vma_struct){.vm_start=FX_OOM,.vm_end=FX_OOM+PGSIZE,.vm_flags=VM_READ|VM_WRITE};
    list_init(&proc_list);
    for (unsigned i=0;i<4;i++) {
        processes[i].pid=i;processes[i].state=PROC_RUNNABLE;
        processes[i].pgdir=(uintptr_t)active_root;
        list_init(&processes[i].list_link);
    }
    processes[2].pid=FX_PID;processes[2].mm=&user_mm;
    processes[3].context.ra=(uintptr_t)worker_entry;
    processes[3].context.sp=(uintptr_t)worker_stack+sizeof(worker_stack);
    list_add_before(&proc_list,&processes[2].list_link);
    list_add_before(&proc_list,&processes[3].list_link);
    idleproc=&processes[0];initproc=&processes[1];current=&processes[2];
    uintptr_t satp=((uintptr_t)SATP_MODE_SV39<<60)|((uintptr_t)active_root>>12);
    write_csr(satp,satp);flush_tlb();asm volatile("fence.i":::"memory");
    idt_init();set_csr(sie,MIP_SSIP);intr_enable();
    assert(fixture_kernel_irq_probe()==1 && read_csr(sscratch)==0);
    passed("S_IRQ_STACK");
    proc_run(&processes[3]);
    assert(current==&processes[2] && worker_runs==1 && processes[3].state==PROC_SLEEPING);
    assert(read_csr(sstatus)&SSTATUS_SIE);passed("CONTEXT_SWITCH");
    intr_disable();
    nested_faults=0;
    clock_set_next_event();set_csr(sie,MIP_STIP);
    enter_user(0);
}
