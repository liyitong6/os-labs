# Lab4 A 版块实验报告：进程创建及初始化

负责人学号：2413524
主要修改文件：`kern/process/proc.c`（标注 `LAB4 function2` 的位置）
提示词：[prompt_A.md](prompt_A.md)

---

## 一、负责范围与交付物

本版块对应 Lab4 功能模块二"进程创建及初始化"，负责把内核从"只有一条执行流"推进到"能够创建并登记多个内核线程"。交付的 6 个函数全部位于 `kern/process/proc.c`：

| 函数 | 职责一句话概括 |
|---|---|
| `alloc_proc` | 分配一块 PCB，并把每个字段置于安全、已知的空白状态 |
| `proc_init` | 初始化进程链表/哈希表，把当前现场包装成 `idleproc`，再创建 `initproc` |
| `kernel_thread` | 把普通内核函数（入口+参数）包装成首次运行所需的中断帧 |
| `do_fork` | 组装 PCB、内核栈、地址空间、中断帧与上下文，登记并唤醒新线程 |
| `copy_thread` | 在新线程内核栈上安放中断帧，并设置首次切换的执行上下文 |
| `copy_mm` | 本阶段地址空间处理的占位接口（内核线程无需复制用户空间） |

---

## 二、对整体逻辑线的理解

Lab4 整体执行链是：

```
kern_init → pmm_init → pic_init/idt_init → vmm_init
          → proc_init → cpu_idle → schedule → proc_run → switch_to
```

前半段（pmm、idt、vmm）准备好物理内存、中断和虚拟地址空间；从 `proc_init` 开始进入进程管理。A 版块解决的是"**线程从哪里来、如何进入系统、如何第一次运行**"，处在链路的 `proc_init` 这一段，并且要为后面 B 版块的调度（`schedule/switch_to`）准备好一个结构完整、可被选中的进程对象。

我把 A 版块内部的逻辑顺序理解为两条线：

1. **idleproc 线**：系统启动后当前这条执行流本身就是一个可运行的内核线程，不需要"复制"产生，而是直接给它分配 PCB、复用启动栈，包装成第 0 个线程 `idleproc`。
2. **initproc 线**：通过 `kernel_thread` 手工构造一个中断帧（因为启动初期没有父进程可复制），再经 `do_fork` 把它变成第 1 个真正的内核线程 `initproc`，其首次执行目标是 `init_main`。

两条线共同保证 `proc_init` 结束时，系统里至少有 `idleproc`（pid=0）和 `initproc`（pid=1），且 `current` 指向 `idleproc`。

---

## 三、对六个核心函数的理解

### 1. alloc_proc
通过 `kmalloc` 申请一块 `proc_struct`，先整体 `memset` 清零，再显式设置关键字段：状态置 `PROC_UNINIT`、pid 置 -1、各指针字段置空、`pgdir` 指向启动页表物理地址、两个链表节点 `list_init`。这样做的意义是**杜绝未初始化字段被后续代码误用**——一个干净、确定的初始状态是后续所有装配步骤的安全基础。分配失败时返回 `NULL`，不产生副作用。

### 2. proc_init
先初始化全局 `proc_list` 和每个哈希桶；然后调用 `alloc_proc` 得到 `idleproc`，它不经过 fork，而是直接复用当前现场：pid=0、状态 `PROC_RUNNABLE`、`kstack` 指向 `bootstack`、`need_resched=1`，并让 `current` 指向它。接着用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 `initproc`，通过 `find_proc` 取回并命名为 `init`。关键对象分配失败时直接 `panic`，避免留下半初始化的子系统。

### 3. kernel_thread
在一个局部中断帧 `tf` 上手工"造"出一次陷入现场：清零后，用被调用者保存寄存器传递信息——`s0` 放目标函数入口、`s1` 放参数；`status` 通过读 `sstatus` 后设置 `SPP`（返回到 S-mode）、`SPIE`（sret 后开中断）并清除 `SIE`；`epc` 设为 `kernel_thread_entry`。最后调用 `do_fork(clone_flags | CLONE_VM, 0, &tf)`，返回新线程 pid。

### 4. do_fork
这是创建线程的主流程，按固定顺序装配：`alloc_proc → setup_kstack → copy_mm → copy_thread`，然后在**关中断的临界区**里完成 `get_pid`、挂接哈希表与进程链表、`nr_process++`，最后 `wakeup_proc` 并返回 pid。关中断是为了防止链表修改与遍历链表的时钟中断并发。失败时通过 goto 阶梯按资源获取的逆序清理（见第五节）。

### 5. copy_thread
在新线程内核栈的最高地址处预留一个中断帧位置，把 `kernel_thread` 准备好的帧复制过去，将 `a0` 置 0（子进程的 fork 返回值）。执行上下文设置为：`context.ra = forkret`、`context.sp = proc->tf`，使首次 `switch_to` 后经 `forkret → forkrets → __trapret` 恢复。此外，中断帧的 `gpr.sp` 被设置为该线程内核栈顶（原因见第六节 bug 分析）。

### 6. copy_mm
本阶段所有线程都是内核线程，`current->mm` 本来就是 `NULL`，没有用户地址空间需要复制。因此该函数仅用 `assert(current->mm == NULL)` 确认前提，并把 `proc->mm` 置空后返回。它保留了完整接口形态，方便后续实验扩展真正的地址空间复制。

---

## 四、三项配套任务

### 1. 衔接 Lab2 依赖
A 版块建立在 Lab2 物理内存管理之上，代码中对 Lab2 接口的调用关系如下：

- `setup_kstack` 调用 `alloc_pages` 分配内核栈，并用 `page2kva` 得到内核虚拟地址；
- 清理路径 `put_kstack` 调用 `kva2page` 与 `free_pages` 回收；
- 新线程 `pgdir` 复用 `boot_pgdir_pa`（启动页表物理地址）。

这些接口名、参数和返回值都与工程中的真实定义一致，A 侧的调用衔接是正确的。需要说明的是：Lab4 框架本身保留了部分 Lab2/Lab3 留空区域，这些不属于 A 的函数；在整组合并时由对应负责人补齐后，整个工程才能完整编译。

### 2. 创建失败与资源清理测试
`do_fork` 用 goto 阶梯保证"占用了多少资源，失败时就释放多少"。清理路径与失败点的对应关系：

| 失败发生在 | 跳转标签 | 清理动作 |
|---|---|---|
| `alloc_proc` 失败 | `fork_out` | 尚未占用资源，直接返回 `-E_NO_MEM` |
| `setup_kstack` 失败 | `bad_fork_cleanup_proc` | `kfree` 释放 PCB |
| `copy_mm` 失败 | `bad_fork_cleanup_kstack` | 先 `put_kstack` 释放内核栈，再 `kfree` 释放 PCB |

标签采用"穿透"写法：`bad_fork_cleanup_kstack` 执行完后自然落入 `bad_fork_cleanup_proc`，保证每个失败点都按资源获取的逆序完整释放，不产生内存泄漏或残留链表结构。正常创建路径在完整工程中通过了官方 `Process Create` 检查。

### 3. PCB 与 PID 分配分析

**PCB（`proc_struct`）**：它是内核对一个线程的全部管理信息的集合，可分为几类字段：
- 身份与生命周期：`state`、`pid`、`runs`、`flags`、`name`；
- 执行资源：`kstack`（内核栈）、`context`（切换上下文）、`tf`（中断帧）、`pgdir`（页目录）；
- 关系与调度：`parent`、`need_resched`、`mm`；
- 链表挂接点：`list_link`（挂全局进程链表）、`hash_link`（挂 pid 哈希表）。

`alloc_proc` 负责把这些字段初始化到安全空白态，`do_fork` 再逐步把它们填成一个可运行实体。

**PID 分配（`get_pid`）**：该函数由框架提供，`do_fork` 在登记阶段调用。它维护 `last_pid`（上一次分配值）与 `next_safe`（在已存在 pid 中、大于当前值的最小占用值）。分配时让 `last_pid` 递增；只要它还小于 `next_safe`，就保证不会与任何现有进程冲突；一旦达到 `next_safe`，就重新扫描整个进程链表，找出新的安全区间并跳过被占用的号。这种"惰性检查 + 冲突时重扫"的方式，既保证 pid 唯一，又避免每次分配都完整遍历链表。

---

## 五、spec 与 prompt 是怎么写的

按照指导书 `lab0.5` 的规范，我把"需求规格（spec）"和"提示词（prompt）"分开处理：

- **spec（行为规格）**：描述每个函数"需要满足什么行为"，而不是"具体怎么实现"，统一使用 `Pre-Condition / Post-Condition / Case / Requirements` 字段，覆盖正常路径、资源不足等不同分支。
- **prompt（提示词）**：采用指导书规定的**四段式结构**组织，见 [prompt_A.md](prompt_A.md)：
  - `[PROMPT]`：明确只改 `proc.c` 的 `LAB4 function2`，强调必须修改真实文件、不碰 B/C 留空区域；
  - `[RELY]`：只放与本任务直接相关、且从工程真实代码核对过的宏、数据结构和可调用函数（遵循"最小、可信"原则）；
  - `[GUARANTEE]`：列出 6 个必须实现的函数签名，作为交付清单；
  - `[SPECIFICATION]`：逐函数给出行为规格。

提示词的粒度是"一个能独立完成一项功能的模块用一个提示词"，因此 6 个函数合在一份提示词里，而不是一函数一份。

---

## 六、使用 AI 生成代码时遇到的 bug 与解决

### Bug 1（核心）：sret 后 sp=0，导致 store access fault
**现象**：内核启动、内存类检查全部通过，`switch_to` 也成功进入 `forkret`，但恢复后 `sret` 进入 `kernel_thread_entry`、再跳入 `init_main` 时，触发 store/amo access fault（RISC-V cause=7），二次进入异常后 panic。

**定位过程**：通过在关键位置加临时打印，观察到 `cpu_idle` 入口和 `forkret` 入口的 sp 都正常，唯独 `sret` 之后 sp 变成 0。结合代码确认链路：
- `kernel_thread` 的局部中断帧经 `memset` 清零，`gpr.sp` 从未被赋值，即为 0；
- `trapentry.S` 的 `RESTORE_ALL` 在 sret 前会执行 `LOAD x2, 2*REGBYTES(sp)`，把 sp 恢复成中断帧里的 `gpr.sp`；
- 而 `kernel_thread_entry` 只有 `move a0,s1; jalr s0; jal do_exit`，**本身不设置栈**。

于是 sret 后 sp=0，`init_main` 第一条 `addi sp,sp,-32` 就在无效栈上压栈而 fault。

**解决**：在 `copy_thread` 中增加一行，让中断帧的 sp 指向该线程自己的内核栈顶：
```c
proc->tf->gpr.sp = proc->kstack + KSTACKSIZE;
```
内核线程 sret 后仍在 S-mode 运行普通 C 代码，需要一个有效内核栈；此时中断帧已被消费、其内存可被复用，所以这样设置既正确又安全。这一行属于 A 版块 `copy_thread` 的内在职责，不是额外功能。

### Bug 2：kernel_thread 函数指针类型与官方测试不匹配
**现象**：运行官方注入测试时，`tools/test_lab4.c` 用 `void (*)(void *)` 类型的线程函数调用 `kernel_thread`，而头文件声明是 `int (*fn)(void *)`，GCC 15 把这种不兼容指针类型直接作为编译错误。

**处理与现状**：指导书与头文件给出的标准签名是 `int (*fn)(void *)`，当前 A 代码遵循该标准签名，`init_main` 也保持 `static int`。这是一个需要在整组合并跑官方评分时注意的接口一致性问题——届时需统一 `kernel_thread` 的函数指针类型与评分测试，使工程能通过编译。

---

## 七、重要知识点及与 OS 原理的对应

| 知识点 | 实验中的体现 | 含义与关系 |
|---|---|---|
| 进程控制块 PCB | `proc_struct` | 内核对线程的全部管理信息的载体，是进程存在的标志 |
| 进程状态机 | `PROC_UNINIT/RUNNABLE/SLEEPING/ZOMBIE` | 描述线程生命周期；本实验主要经历 UNINIT→RUNNABLE |
| 上下文 vs 中断帧 | `context` 与 `trapframe` | context 只保存 14 个 callee-saved 寄存器用于线程切换；trapframe 保存更完整的陷入现场 |
| 特权级返回 | `sret`、`sstatus` 的 SPP/SPIE/SIE | SPP 决定返回到哪个特权级，SPIE 决定返回后是否开中断 |
| 内核线程 | 共享内核地址空间的线程 | 只在内核态运行、共享内核资源，因此 `mm` 为空、复用内核页表 |
| 复制创建语义 | `do_fork`、`CLONE_VM` | 新进程通过复制父进程状态建立；CLONE_VM 表示共享地址空间 |
| 资源分配与回收 | goto 清理阶梯、`setup/put_kstack` | 失败路径必须释放已占用资源，保证系统状态一致 |
| pid 唯一性 | `get_pid` | 通过安全区间与冲突重扫保证分配的 pid 不重复 |

---

## 八、OS 原理中重要、但本实验未对应上的知识点

1. **用户态进程与特权级切换到 U-mode**：本实验的线程都是内核线程，sret 始终回到 S-mode；用户进程在 Lab5 才涉及。
2. **真正的地址空间复制**：`copy_mm` 目前是占位实现，没有独立用户地址空间的复制与隔离。
3. **进程退出与回收**：`do_exit` 是占位 `panic`，`do_wait`、僵尸进程（ZOMBIE）回收机制在本实验未实现。
4. **时间片抢占与优先级调度**：当前时钟中断只维护计数，没有基于时间片强制抢占；调度是简单的链表选择，没有优先级策略。
5. **多核并发与同步锁**：临界区仅针对单核中断开关，不涉及多核环境下的锁与互斥。
6. **完整的阻塞/唤醒语义**：`PROC_SLEEPING` 状态及配套的阻塞唤醒链路在本实验没有完整体现。

---


