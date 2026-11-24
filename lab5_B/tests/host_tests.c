#include "include/host.h"
#include <stdarg.h>

struct proc_struct *current,*idleproc,*initproc;
list_entry_t proc_list;
struct mm_struct *check_mm_struct;
volatile size_t ticks;
uintptr_t host_sstatus,host_satp,host_sip,host_sscratch,host_stvec;
static struct proc_struct pool[7];
static struct Page pages[16];
static pte_t table1[8],table2[8];
static struct mm_struct mm;
static struct trapframe tf;
static jmp_buf jump;
static int expect_exit,exit_error,backend_calls,backend_kind,backend_value;
static uintptr_t backend_args[4];
static int fail_alloc,fail_insert,allocs,frees,flushes,invalidates,switches,timers;
static int printed_char,pgdir_prints,checks;

int cprintf(const char *s,...) { (void)s; return 0; }
void warn(const char *s,...) { (void)s; }
void host_panic(const char *s,...) { (void)s; longjmp(jump,2); }
void cputchar(int c) { printed_char=c; }
void print_pgdir(void) { pgdir_prints++; }
void clock_set_next_event(void) { timers++; }
void __alltraps(void) {}
void flush_tlb(void) { flushes++; }
void tlb_invalidate(pde_t *p,uintptr_t va) { (void)p;(void)va;invalidates++; }
pte_t *get_pte(pde_t *p,uintptr_t va,bool create) {
    (void)create;
    if (!p || va<USERBASE || va>=USERBASE+8*PGSIZE) return NULL;
    return &p[(va-USERBASE)/PGSIZE];
}
static pte_t encode(struct Page *p,unsigned perm) {
    return ((uintptr_t)(p-pages+1)<<10)|perm;
}
struct Page *pte2page(pte_t p) {
    size_t n=(p>>10)-1; assert(n<16); return &pages[n];
}
void *page2kva(struct Page *p) { return p->data; }
struct Page *host_alloc_page(void) {
    allocs++;
    if(fail_alloc) return NULL;
    for(int i=0;i<16;i++) if(!pages[i].allocated) {
        pages[i].allocated=1; pages[i].ref=0; return &pages[i];
    }
    return NULL;
}
void host_free_page(struct Page *p) {
    assert(p->allocated && p->ref==0); p->allocated=0; frees++;
}
int page_insert(pde_t *p,struct Page *page,uintptr_t va,uint32_t perm) {
    if(fail_insert) return -E_NO_MEM;
    pte_t *e=get_pte(p,va,0); if(!e) return -E_NO_MEM;
    page->ref++;
    if(*e&PTE_V) {
        struct Page *old=pte2page(*e);
        assert(old->ref>0); if(--old->ref==0) host_free_page(old);
    }
    *e=encode(page,perm|PTE_V); tlb_invalidate(p,va); return 0;
}
bool user_mem_check(struct mm_struct *m,uintptr_t addr,size_t len,bool write) {
    return m && len && m->start<=addr && addr<m->end && len<=m->end-addr &&
           (!write || m->writable);
}
int do_exit(int e) {
    exit_error=e; backend_kind=SYS_exit;backend_calls++;
    if(expect_exit) longjmp(jump,1); return backend_value;
}
int do_fork(uint32_t flags,uintptr_t sp,struct trapframe *t) {
    backend_calls++;backend_kind=SYS_fork;
    backend_args[0]=flags;backend_args[1]=sp;backend_args[2]=(uintptr_t)t;
    return backend_value;
}
int do_wait(int pid,int *p) {
    backend_calls++;backend_kind=SYS_wait;backend_args[0]=pid;backend_args[1]=(uintptr_t)p;
    return backend_value;
}
int do_execve(const char *name,size_t len,unsigned char *bin,size_t size) {
    backend_calls++;backend_kind=SYS_exec;backend_args[0]=(uintptr_t)name;
    backend_args[1]=len;backend_args[2]=(uintptr_t)bin;backend_args[3]=size;
    return backend_value;
}
struct proc_struct *find_proc(int pid) {
    for(int i=0;i<7;i++) if(pool[i].pid==pid && pid>0) return &pool[i];
    return NULL;
}
void switch_to(struct context *from,struct context *to) {
    (void)from; assert(to==&current->context);switches++;
    assert(!(host_sstatus&SSTATUS_SIE));
}

/* Execute the real production C functions; only hardware/backends are mocked. */
#include "../overlay/kern/mm/cow.c"
#include "../overlay/kern/syscall/syscall.c"
#include "../overlay/kern/trap/trap.c"
#include "../overlay/kern/schedule/sched.c"
#include "../snippets/proc_B.c"

#define CHECK(x) do { checks++; if(!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
static void reset(void) {
    memset(pool,0,sizeof(pool));memset(pages,0,sizeof(pages));
    memset(table1,0,sizeof(table1));memset(table2,0,sizeof(table2));memset(&tf,0,sizeof(tf));
    list_init(&proc_list);
    for(int i=0;i<7;i++) { pool[i].pid=i;pool[i].state=PROC_RUNNABLE;list_init(&pool[i].list_link); }
    idleproc=&pool[0];initproc=&pool[1];current=&pool[2];current->tf=&tf;
    mm=(struct mm_struct){table1,USERBASE,USERBASE+8*PGSIZE,1};current->mm=&mm;
    host_sstatus=SSTATUS_SIE;host_satp=host_sip=host_sscratch=host_stvec=0;
    backend_calls=backend_kind=0;backend_value=37;expect_exit=0;exit_error=0;
    fail_alloc=fail_insert=allocs=frees=flushes=invalidates=switches=timers=0;
    pgdir_prints=printed_char=0;ticks=0;
}
static void call(unsigned long number) { tf.gpr.a0=number;syscall(); }
static void syscall_tests(void) {
    reset();
    unsigned long bad[]={0,5,11,17,19,29,32,~0UL};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        call(bad[i]);CHECK((intptr_t)tf.gpr.a0==-E_INVAL);CHECK(backend_calls==0);
    }
    call(SYS_getpid);CHECK(tf.gpr.a0==2);
    tf.gpr.sp=USERBASE+123;call(SYS_fork);
    CHECK(tf.gpr.a0==37 && backend_kind==SYS_fork);
    CHECK(backend_args[0]==0 && backend_args[1]==tf.gpr.sp && backend_args[2]==(uintptr_t)&tf);
    tf.gpr.a1=(uintptr_t)-42;call(SYS_exit);CHECK(exit_error==-42);
    tf.gpr.a1=3;tf.gpr.a2=0;call(SYS_wait);CHECK(backend_kind==SYS_wait && backend_args[1]==0);
    int before=backend_calls;tf.gpr.a2=USERTOP-2;call(SYS_wait);
    CHECK((intptr_t)tf.gpr.a0==-E_INVAL && backend_calls==before);
    tf.gpr.a2=USERBASE+64;call(SYS_wait);CHECK(backend_calls==before+1);
    tf.gpr.a1=USERBASE;tf.gpr.a2=4;tf.gpr.a3=USERBASE+128;tf.gpr.a4=256;
    call(SYS_exec);CHECK(backend_kind==SYS_exec && backend_args[3]==256);
    before=backend_calls;tf.gpr.a4=~(uintptr_t)0;call(SYS_exec);
    CHECK((intptr_t)tf.gpr.a0==-E_INVAL && backend_calls==before);
    tf.gpr.a1='Z';call(SYS_putc);CHECK(printed_char=='Z' && tf.gpr.a0==0);
    call(SYS_pgdir);CHECK(pgdir_prints==1 && tf.gpr.a0==0);
    call(SYS_yield);CHECK(current->need_resched && tf.gpr.a0==0);
    tf.gpr.a1=3;call(SYS_kill);CHECK((pool[3].flags&PF_EXITING) && tf.gpr.a0==0);
    tf.cause=CAUSE_USER_ECALL;tf.epc=0x1000;tf.gpr.a0=SYS_getpid;
    exception_handler(&tf);CHECK(tf.epc==0x1004 && tf.gpr.a0==2);
}
static void kill_tests(void) {
    reset();CHECK(do_kill(-1)==-E_INVAL);CHECK(do_kill(0)==-E_INVAL);CHECK(do_kill(1)==-E_INVAL);
    pool[3].state=PROC_SLEEPING;pool[3].wait_state=WT_CHILD;
    CHECK(do_kill(3)==0);CHECK(pool[3].state==PROC_RUNNABLE && pool[3].wait_state==0);
    CHECK(do_kill(3)==-E_KILLED);
    pool[4].state=PROC_SLEEPING;pool[4].wait_state=1;
    CHECK(do_kill(4)==0 && pool[4].state==PROC_SLEEPING);
    pool[5].state=PROC_ZOMBIE;CHECK(do_kill(5)==-E_INVAL);
    pool[6].state=PROC_UNINIT;CHECK(do_kill(6)==-E_INVAL);
    CHECK(host_sstatus&SSTATUS_SIE);
    host_sstatus=0;CHECK(do_kill(99)==-E_INVAL && host_sstatus==0);
}
static void scheduler_tests(void) {
    for(unsigned mask=0;mask<64;mask++) for(int at=0;at<7;at++) for(int enabled=0;enabled<2;enabled++) {
        reset();
        for(int i=1;i<7;i++) {
            list_add_tail(&proc_list,&pool[i].list_link);
            pool[i].state=(mask&(1u<<(i-1)))?PROC_RUNNABLE:PROC_SLEEPING;
            pool[i].pgdir=(uintptr_t)(0x80400000+i*PGSIZE);
        }
        current=&pool[at];current->need_resched=1;struct proc_struct *old=current;
        host_sstatus=enabled?SSTATUS_SIE:0;
        int expected=0;
        for(int step=1;step<=6;step++) {
            int candidate=(at==0)?step:1+(at-1+step)%6;
            if(mask&(1u<<(candidate-1))) { expected=candidate;break; }
        }
        schedule();CHECK(current==&pool[expected]);CHECK(!old->need_resched);
        CHECK(current->runs==1);CHECK(!!(host_sstatus&SSTATUS_SIE)==enabled);
        CHECK(switches==(old!=current));
        if(old!=current) CHECK(host_satp==((UINT64_C(8)<<60)|(current->pgdir>>12)));
    }
}
static void seed_shared(int refs,unsigned flags) {
    pages[0].allocated=1;pages[0].ref=refs;
    for(int i=0;i<PGSIZE;i++)pages[0].data[i]=(unsigned char)(i*17+9);
    table1[0]=encode(&pages[0],PTE_V|PTE_R|PTE_U|flags);
}
static void cow_tests(void) {
    reset();seed_shared(2,PTE_COW|PTE_X);table2[0]=table1[0];
    pte_t parent=table2[0];CHECK(cow_resolve_write(&mm,USERBASE+13)==0);
    struct Page *child=pte2page(table1[0]);
    CHECK(child!=&pages[0] && child->ref==1 && pages[0].ref==1);
    CHECK(memcmp(child->data,pages[0].data,PGSIZE)==0 && table2[0]==parent);
    CHECK((table1[0]&(PTE_W|PTE_X))==(PTE_W|PTE_X) && !(table1[0]&PTE_COW));
    child->data[9]^=1;CHECK(child->data[9]!=pages[0].data[9]);
    mm.pgdir=table2;int old_allocs=allocs;
    CHECK(cow_resolve_write(&mm,USERBASE)==0 && allocs==old_allocs);
    CHECK(pte2page(table2[0])==&pages[0] && !(table2[0]&PTE_COW));
    reset();seed_shared(2,PTE_COW);pte_t old=table1[0];fail_alloc=1;
    CHECK(cow_resolve_write(&mm,USERBASE)==-E_NO_MEM && table1[0]==old && pages[0].ref==2);
    fail_alloc=0;fail_insert=1;
    CHECK(cow_resolve_write(&mm,USERBASE)==-E_NO_MEM && table1[0]==old && pages[0].ref==2 && frees==1);
    reset();seed_shared(1,0);old=table1[0];CHECK(cow_resolve_write(&mm,USERBASE)==-E_INVAL && table1[0]==old);
    table1[0]|=PTE_COW;mm.writable=0;
    CHECK(cow_resolve_write(&mm,USERBASE)==-E_INVAL && !(table1[0]&PTE_W));
    mm.writable=1;table1[0]&=~PTE_U;CHECK(cow_resolve_write(&mm,USERBASE)==-E_INVAL);
    CHECK(cow_resolve_write(NULL,USERBASE)==-E_INVAL);
    reset();seed_shared(1,PTE_COW);table1[1]=encode(&pages[0],PTE_V|PTE_R|PTE_U|PTE_COW);pages[0].ref=2;
    CHECK(cow_prepare_write(&mm,USERBASE+PGSIZE-2,4)==0);
    CHECK((table1[0]&PTE_W) && (table1[1]&PTE_W));
    CHECK(pte2page(table1[0])!=pte2page(table1[1]));
    CHECK(cow_prepare_write(&mm,USERTOP-1,~(size_t)0)==-E_INVAL);
    CHECK(cow_prepare_write(NULL,0,0)==0);
    reset();seed_shared(1,PTE_COW);host_sstatus=0;
    CHECK(cow_resolve_write(&mm,USERBASE)==0 && host_sstatus==0);
    reset();seed_shared(1,PTE_COW);table1[1]=0;
    CHECK(cow_prepare_write(&mm,USERBASE+PGSIZE-2,4)==-E_INVAL);
    CHECK((table1[0]&PTE_W) && pages[0].data[0]==9);
}
static void trap_tests(void) {
    reset();struct trapframe nested={0};struct trapframe *saved=current->tf;
    nested.cause=CAUSE_USER_ECALL;nested.status=SSTATUS_SPP;nested.gpr.a0=SYS_getpid;
    trap(&nested);CHECK(current->tf==saved && nested.epc==4 && nested.gpr.a0==2);
    reset();seed_shared(1,PTE_COW);tf.cause=CAUSE_STORE_PAGE_FAULT;tf.tval=USERBASE;tf.epc=0x8800;
    exception_handler(&tf);CHECK(tf.epc==0x8800 && (table1[0]&PTE_W));
    reset();seed_shared(1,PTE_COW);tf.cause=CAUSE_STORE_PAGE_FAULT;tf.tval=USERBASE;tf.status=SSTATUS_SPP;
    exception_handler(&tf);CHECK(table1[0]&PTE_W);
    reset();expect_exit=1;tf.cause=CAUSE_LOAD_PAGE_FAULT;
    int code=setjmp(jump);if(code==0)exception_handler(&tf);
    CHECK(code==1 && exit_error==-E_KILLED);
    reset();tf.status=SSTATUS_SPP;tf.cause=CAUSE_ILLEGAL_INSTRUCTION;
    code=setjmp(jump);if(code==0)exception_handler(&tf);CHECK(code==2);
    reset();seed_shared(2,PTE_COW);fail_alloc=1;expect_exit=1;
    tf.status=SSTATUS_SPP;tf.cause=CAUSE_STORE_PAGE_FAULT;tf.tval=USERBASE;
    code=setjmp(jump);if(code==0)exception_handler(&tf);CHECK(code==1 && exit_error==-E_NO_MEM);
    reset();struct trapframe irq={0};irq.status=SSTATUS_SPP;irq.cause=(UINT64_C(1)<<63)|IRQ_S_TIMER;
    trap(&irq);CHECK(timers==1 && ticks==1 && current->need_resched && switches==0);
    host_sip=MIP_SSIP;irq.cause=(UINT64_C(1)<<63)|IRQ_S_SOFT;
    trap(&irq);CHECK(!(host_sip&MIP_SSIP));
}
int main(void) {
    CHECK(sizeof(struct context)==112);CHECK(sizeof(struct trapframe)==288);
    syscall_tests();puts("PASS syscall dispatch, ABI forwarding, pointer bounds and injected ECALL behavior");
    kill_tests();puts("PASS yield/kill lifecycle flags, protected processes, interruptible wakeup and interrupt state");
    scheduler_tests();puts("PASS 896 scheduler scenarios (runnable sets, current thread and interrupt state)");
    cow_tests();puts("PASS COW isolation, last-reference path, permissions, rollback and cross-page user writes");
    trap_tests();puts("PASS nested trap frame restoration, user/kernel fault policy, COW retry, timer and software IRQ");
    printf("PASS %d assertions against real B C sources with mocked platform/backends\n",checks);
    return 0;
}
