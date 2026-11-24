# A / C 对接约定

## 所有权

| B 已完成 | A / C 后续提供 |
|---|---|
| `kern/syscall/syscall.c`、`kern/trap/trap.c`、`trapentry.S` | A 的装载、退出与等待；C 的 fork 与地址空间复制 |
| `cpu_idle/proc_run/do_yield/do_kill`、`sched.c`、`switch.S` | A 的 PCB 初始化、`proc_init/kernel_thread`；C 的 `copy_thread` |
| `cow.h/cow.c` 中的写缺页和写前准备 | C 的 COW 共享映射、引用计数及内存辅助函数接入 |
| 修正的用户库系统调用封装 | C 的物理页分配、页映射与回收依赖 |

`proc.c` 必须按函数合并。B 的安装器已为此处理，不用覆盖整个文件。

## 固定 ABI 与页表

- 用户系统调用号放在 `a0`，参数为 `a1..a5`，返回值写回 `a0`；这是本实验自定义约定。
- ECALL 返回地址增加 4；COW 写故障不增加 `epc`，必须重试原写指令。
- `proc->pgdir` 为根页表物理字节地址；`mm->pgdir` 为内核可访问的页表指针。内核线程使用有效的内核根页表地址。
- `sys_fork` 调用 `do_fork(0, current->tf->gpr.sp, current->tf)`；C 的子进程返回值为 0，父进程得到 PID。
- `copy_thread` 必须在用户 fork 中保持用户栈指针；创建内核线程时，最终恢复的 `tf->gpr.sp` 应是有效内核栈位置，不能为 0。
- 共享的内核映射须覆盖切换代码、PCB 和所有内核栈。

## 首次用户态：A 与 B

A 默认沿指导书的显式恢复路径执行：`kernel_execve` 完成装载后，把栈切到合法 trapframe 并进入 `__trapret`。B 已提供相应恢复宏。用户 frame 要有：

- `epc` 为 ELF 用户入口；`gpr.sp` 为用户栈顶。
- `status.SPP = 0`、`status.SPIE = 1`，恢复过程中保持 `SIE = 0`；保留所需 SUM 等内核访问约定。
- 恢复用 frame 位于当前线程内核栈上，恢复宏以 `frame + 288` 作为下次 U→S Trap 的内核栈位置。

恢复到 S 态时 B 清零 `sscratch`，恢复到 U 态时保存内核栈位置。新增 `__lab5_sret` 标签只用于调试定位。

课堂讲的 EBREAK 复用方案可以解释比较，但本版本没有把任意 S 态断点当 exec。A 如更改启动路线，需一并提交明确的异常约定。

`sys_exec` 校验的是用户 name 和 binary 范围。A 必须在释放旧 `mm` 前妥善暂存用户来源的必要输入，防止校验后把仍要读的旧映像释放。内核首次启动直接走 A 的 `do_execve` 路线，不受用户系统调用封装的范围限制。

## COW：C 必须使用相同标记

```c
#include <cow.h>
/* PTE_COW == 0x100, Sv39 的 RSW bit 8 */
```

C 的 fork 共享路径：只对原本允许写入的用户映射，以及已经带 COW 标记的映射，清除 `PTE_W` 并保留/设置 `PTE_COW`；父子映射都要更新，并维护物理页映射引用计数及 TLB。真正只读的程序页不能伪装成 COW。原 VMA 的 `VM_WRITE` 语义应保留。

B 已实现：

```c
int cow_resolve_write(struct mm_struct *mm, uintptr_t addr);
int cow_prepare_write(struct mm_struct *mm, uintptr_t addr, size_t len);
```

返回 0 为成功；负值为失败。`cow_resolve_write` 只处理合法、有效、用户可读、带 COW 且尚不可写的页；引用为 1 时直接恢复写权限，否则复制一页并替换当前映射。共享页由 `page_insert` 更新新旧引用及 TLB，失败时须保持旧映射完整。

C 的 `copy_to_user` 在 `memcpy` 前调用 `cow_prepare_write`，成功后才能写。A 的 `do_wait` 回写退出码也使用这个统一路径，避免绕过 COW。即使从内核触发合法 COW 写缺页，B 也能处理后重试；显式写前准备能把失败作为错误返回给调用者。

跨页准备如果在后页失败，前页可能已变成私有可写页，但尚未由准备函数写入任何用户字节；调用者不得在返回失败后继续 memcpy。

本版本按当前单 hart QEMU 配置，以关本地中断保护 PTE/引用更新。没有声明实现多核锁和跨核 TLB shootdown。

## 生命周期及框架缺口

- B 的 `do_kill` 对有效目标设置 `PF_EXITING`，唤醒可中断睡眠；不存在/不合适的目标返回 `-E_INVAL`，已标记退出返回 `-E_KILLED`。idle/init 被保护。
- `do_yield` 只设置 `need_resched`，由现有 `trap()` 在用户返回边界执行调度，满足官方注入检查。
- 时钟中断也请求重新调度，让 `spin` 子进程可以被父进程终止。保留原框架的 `print_ticks/DEBUG_GRADE` 钩子。
- A 的 `do_exit` 不应返回；完成用户地址空间回收、僵尸状态、唤醒及调度。B 对用户非法访问结束当前进程，内核意外异常仍停机诊断。
- A 的 `do_execve/do_exit` 在释放正在使用的旧页表前，应按 Sv39 约定切回有效内核页表并同步 TLB；不要先释放 CPU 仍在使用的页表，再期待 B 的下次调度修复。
- 当前老师框架只声明了 `print_pgdir()`，没有定义。B 的 `sys_pgdir` 会调用它；C 在页表依赖中补齐该辅助函数，否则完整链接会失败。
- A/C 的 `YOUR CODE` 区域保留，不能把它们算作 B 未完成或把主机专项当成整组评分。
