# Lab3 异常处理实现 Prompt

## 一：异常分派与返回位置更新

### [PROMPT]

在当前 uCore 工程的 `kern/trap/trap.c` 中实现 `void exception_handler(struct trapframe *tf)`，处理非法指令、断点及其他异常。工程目录为 `D:\OS-Lab\projects\lab3\code`，WSL 路径为 `/mnt/d/OS-Lab/projects/lab3/code`。

先读取相关源码，再修改真实文件。仅在 `trap_dispatch()` 之前新增该函数及 `lab3 function2 2413646` 归属注释，保留 B 的代码、现有 `YOUR CODE` 注释和其他本地修改。不修改其他函数、文件、接口或结构体，不添加时钟关机逻辑。

完成后使用现有 WSL 工具链进行普通编译、链接检查，不新增或运行功能测试、QEMU 或整体评分，不修改构建参数绕过错误。

### [RELY]

读取 `kern/trap/trap.c`、`trap.h`、`trapentry.S`、`libs/riscv.h`、`defs.h`，按需核对输出函数实现，以真实定义为准。

`struct trapframe` 包含 `gpr`、`status`、`epc`、`badvaddr`、`cause`。现有 `trap_dispatch()` 已将异常交给本函数；`RESTORE_ALL` 会把处理后的 `tf->epc` 写回 `sepc`，随后执行 `sret`。

```c
#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT          0x3

void print_trapframe(struct trapframe *tf);
```

现有材料可确认输出前缀为 `Illegal instruction at` 和 `Breakpoint at`，本次采用下述完整格式。当前 `uintptr_t` 底层为 `unsigned long long`，使用 `%lx` 时显式转换为 `unsigned long`。

### [GUARANTEE]

提供完整的 `exception_handler()` 定义，使现有调用链可编译、链接。非法指令和断点分支只更新 `tf->epc`，其他 frame 字段保持不变；其他异常只打印现场。处理完成后返回统一 Trap 返回流程，不直接写 `sepc` 或执行 `sret`。

### [SPECIFICATION]

#### Pre-Condition

`tf` 是有效异常现场。恢复范围为实验涉及的 16 位、32 位指令；对于非法指令和断点，`tf->epc` 指向至少二字节对齐、开头半字可读的指令起点。不把 `tf->badvaddr` 无条件当作指令编码。

#### Case 1：非法指令

输出修改前的异常地址：

```c
cprintf("Illegal instruction at 0x%016lx\n", (unsigned long)tf->epc);
```

根据指令长度将 `tf->epc` 增加 2 或 4，然后返回。

#### Case 2：断点

输出修改前的异常地址：

```c
cprintf("Breakpoint at 0x%016lx\n", (unsigned long)tf->epc);
```

同样按实际长度将 `tf->epc` 增加 2 或 4，兼容压缩断点与普通断点，不能根据异常类型固定偏移。

#### Case 3：其他异常

调用一次 `print_trapframe(tf)` 后返回，不修改 frame，也不读取 `tf->epc` 指向的内存。兜底仅用于诊断，不实现未知异常恢复。

#### Requirements

两类已处理异常均从 `tf->epc` 读取首个 `uint16_t` 判断长度：bits[1:0] 不等于 `11` 表示 2 字节；bits[1:0] 等于 `11` 且 bits[4:2] 不等于 `111` 表示 4 字节。本任务不扩展到超过 32 位的指令编码。

每次只推进一次 `epc`，使返回位置指向下一条指令。不要用一次 32 位读取代替必要的半字读取，也不要硬编码测试地址、操作码或按测试宏改变行为。

函数内部完成逻辑，不新增辅助函数、全局状态、panic、关机或死循环。构建失败时定位并记录原因，不修改范围外的代码使其通过。

## 时钟中断的累计输出与关机

### [PROMPT]

在 `D:\OS-Lab\projects\lab3\code` 中补齐时钟关机逻辑，直接修改真实代码并验证。只修改 `kern/trap/trap.c` 的必要头文件、关机阈值定义和 `interrupt_handler()` 的时钟分支，保留其余函数及已有修改。

### [RELY]

现有时钟分支每次调用 `clock_set_next_event()`、增加 `ticks`，每 `TICK_NUM=100` 次调用 `print_ticks()`。关机接口为 `libs/sbi.h` 中的 `sbi_shutdown()`。

### [GUARANTEE]

新增可由编译宏覆盖的 `SHUTDOWN_AFTER`，表示累计统计输出次数。当前材料没有给出数值，暂定默认值为 10，即 1000 次时钟中断后关机；该值是实现默认值，不冒称课程指定值。

### [SPECIFICATION]

每次时钟中断仍先设置下一次事件，再将 `ticks` 增加一次。达到 `TICK_NUM` 的整数倍时先打印统计信息；累计输出次数达到 `SHUTDOWN_AFTER` 后调用 `sbi_shutdown()`。不要按评分宏改变功能行为，不修改异常处理或其他分支。

使用已有工具链编译，并验证正常启动后恰好输出 10 次 `100 ticks` 后 QEMU 自动退出。再运行现有 `make grade`，记录真实结果；发现测试或实现问题时如实定位，不修改判据使其通过。不更新学习笔记或报告。
