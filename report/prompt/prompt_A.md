# Lab4 A 版块：进程创建及初始化

## [PROMPT]

**任务**：修改文件 `kern/process/proc.c`，完成"进程创建及初始化"功能模块，建立从一块空白进程控制块（PCB）到一个可被调度器选中运行的内核线程的完整创建链路。具体包括：分配并初始化 PCB、把当前正在执行的内核现场包装为第 0 个内核线程 `idleproc`、创建第一个真正的内核线程 `initproc`，以及为新线程装配首次运行所需的中断帧（trapframe）与执行上下文（context）。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码片段。保留文件中的现有代码与框架注释，只在标注 `LAB4 function2` 的位置补全；不得删除既有逻辑，不得改动 `LAB4 function1`（C 版块页操作）与 `LAB4 function3`（B 版块调度）负责的留空区域。

**输出要求**：直接进行文件操作，使用下面 `[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]` 部分提供的信息，以工程中的真实代码为准，不要引入与本功能无关的接口或行为。

---

## [RELY]

### 相关宏定义

```c
// kern/process/proc.h
#define PROC_NAME_LEN 15
#define MAX_PROCESS   4096
#define MAX_PID       (MAX_PROCESS * 2)

// 进程生命周期状态
enum proc_state {
    PROC_UNINIT = 0,
    PROC_SLEEPING,
    PROC_RUNNABLE,
    PROC_ZOMBIE,
};

// kern/mm/memlayout.h
#define KSTACKPAGE 2
#define KSTACKSIZE (KSTACKPAGE * PGSIZE)   // PGSIZE = 4096

// kern/mm/pmm.h
#define CLONE_VM     0x00000100
#define CLONE_THREAD 0x00000200

// libs/riscv.h
#define SSTATUS_SIE  0x00000002
#define SSTATUS_SPIE 0x00000020
#define SSTATUS_SPP  0x00000100

// libs/error.h
#define E_NO_MEM 4
```

### 关键数据结构

```c
// 进程控制块
struct proc_struct {
    enum proc_state state;
    int pid;
    int runs;
    uintptr_t kstack;
    volatile bool need_resched;
    struct proc_struct *parent;
    struct mm_struct *mm;
    struct context context;          // switch_to 恢复的寄存器上下文
    struct trapframe *tf;            // 当前中断帧
    uintptr_t pgdir;                 // 页目录（satp 期望物理地址）
    uint32_t flags;
    char name[PROC_NAME_LEN + 1];
    list_entry_t list_link;
    list_entry_t hash_link;
};

// 被调用者保存的切换上下文
struct context {
    uintptr_t ra, sp;
    uintptr_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

// 中断帧：通用寄存器 + 陷入状态
struct trapframe {
    struct pushregs gpr;            // 含 sp、s0、s1、a0 等
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
```

### 可依赖的函数与全局符号

- 内存与字符串：`kmalloc` / `kfree`、`memset`；
- 链表：`list_init` / `list_add`；
- 进程辅助（本文件已提供）：`setup_kstack` / `put_kstack`、`get_pid`、`hash_proc`、`find_proc`、`set_proc_name`；
- 调度模块提供：`wakeup_proc`（`kern/schedule/sched.c`）；
- 临界区与寄存器：`local_intr_save` / `local_intr_restore`、`read_csr`；
- 汇编入口：`kernel_thread_entry`、`forkrets`、`forkret`；
- 全局变量与符号：`proc_list`、`hash_list`、`nr_process`、`current`、`idleproc`、`initproc`、`boot_pgdir_pa`、`bootstack`。

---

## [GUARANTEE]

必须实现的主要接口（签名须与 `proc.h` 中的声明一致）：

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

允许根据需要添加模块内部的辅助函数（应设为 `static`）、内部宏或静态变量；不得改变上述公开接口，也不得改动 B、C 版块的函数。

---

## [SPECIFICATION]

### alloc_proc

**Pre-Condition**：内核内存分配器已可使用，允许调用 `kmalloc`。

**Post-Condition**：返回一个每个字段都处于已知安全状态的 PCB；若无法获得内存则返回空指针，且不产生任何副作用。

**Case 1（分配成功）**：得到一块 PCB，其生命周期状态为未初始化、进程标识为无效值、各类指针字段为空、页目录指向启动页表、两个链表节点均已初始化，返回该 PCB；调用方不应观察到任何未初始化字段。

**Case 2（分配失败）**：内存不足时返回空指针，不修改任何全局状态。

**Requirements**：禁止返回含未初始化字段的 PCB；该函数只负责构造对象，不负责登记或唤醒。

---

### proc_init

**Pre-Condition**：进程子系统尚未初始化，系统中还没有已登记的进程，进程链表与哈希表有待建立。

**Post-Condition**：进程链表与全部哈希桶初始化完成；系统中存在 `idleproc`（pid 为 0）与 `initproc`（pid 为 1），`current` 指向 `idleproc`，且 `initproc` 已具备被调度运行的条件。

**Case 1（包装 idleproc）**：`idleproc` 不通过 fork 创建，而是直接复用当前执行现场与启动栈，标记为可运行并置上调度请求，使其成为当前进程。

**Case 2（创建 initproc）**：通过内核线程机制创建一个执行 `init_main` 的线程，完成登记后将其命名为 `init`。

**Requirements**：任一关键对象分配失败都必须通过 `panic` 终止，而不能留下半初始化、状态不一致的进程子系统。

---

### kernel_thread

**Pre-Condition**：调用方给出目标内核函数、参数与克隆标志；当前处于可读取状态寄存器的内核态。

**Post-Condition**：目标函数与参数被包装成线程首次运行所需的中断帧，并经 `do_fork` 产生一个内核线程，返回新线程的 pid。

**Requirements**：中断帧必须表达"返回到 S-mode、`sret` 后中断开启、首次进入 `kernel_thread_entry`"这一状态；目标函数入口与其参数通过被调用者保存寄存器传递，由 `kernel_thread_entry` 读取。

---

### do_fork

**Pre-Condition**：调用方提供克隆标志、栈位置以及已准备好的中断帧。

**Post-Condition**：成功时返回新线程的 pid，且该线程已完成登记并处于可运行状态；失败时返回负错误码，并且此前已获取的全部资源被完整释放，系统无资源泄漏、无残留链表结构。

**Case 1（完整成功路径）**：依次完成 PCB、内核栈、地址空间、中断帧与执行上下文的准备，随后在中断关闭的临界区内分配 pid、挂接哈希表与进程链表并更新进程计数，最后将线程唤醒并返回其 pid。

**Case 2（中途资源不足）**：任一准备步骤失败时，按已获取资源的逆序清理，再返回错误码。

**Requirements**：对进程链表、哈希表等全局结构的修改必须在中断关闭的临界区内完成，以避免与遍历这些结构的中断并发。

---

### copy_thread

**Pre-Condition**：新线程已经拥有自己的内核栈，且传入了待复制的中断帧。

**Post-Condition**：中断帧被安放在该线程内核栈的高地址区域，子进程的 fork 返回值为 0；执行上下文被设置为首次切换时经由 `forkret` 完成恢复，并且 `sret` 之后该线程拥有一个有效的内核栈。

**Requirements**：执行上下文的返回地址须指向 `forkret`、栈指针须指向中断帧；由于 `kernel_thread_entry` 本身不设置栈，中断帧中的栈指针必须被设置为该线程内核栈上的有效位置，否则 `sret` 后将在无效栈上运行。

---

### copy_mm

**Pre-Condition**：本阶段所有线程均为内核线程，当前进程的内存管理结构为空。

**Post-Condition**：新线程的内存管理字段保持为空，函数返回成功。

**Requirements**：本实验阶段仅维持接口形态，不执行真正的用户地址空间复制；若前置条件不满足，应通过断言暴露而不是静默继续，以便后续实验在此基础上扩展。
