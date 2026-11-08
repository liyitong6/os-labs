# Lab3 功能模块二任务（B）：中断处理 interrupt_handler 提示词

```text
[PROMPT]
任务：在 riscv64-ucore 内核的 kern/trap/trap.c 中，补全 S 态中断处理函数
void interrupt_handler(struct trapframe *tf);

背景：trap_dispatch() 已经按 tf->cause 最高位把事件分派好——最高位=1 是中断，调本函数；
最高位=0 是异常，调同文件的 exception_handler()（由另一名同学负责，你不要实现、不要改）。
你只负责"中断"这一支。

要做的事：
1) 在 kern/trap/trap.c 中标注 //lab3 function2 YOUR CODE 的位置，实现完整的
   interrupt_handler(struct trapframe *tf)。
2) 该文件已 #include <clock.h>、<riscv.h>、<stdio.h>、<trap.h>，所需符号都在，
   不要新增 include、不要改 Makefile、不要动其他文件。

操作要求：
- 只新增 interrupt_handler 这一个函数，放在 //lab3 function2 YOUR CODE 注释下方。
- 一个字符都不要改：idt_init、trap_in_kernel、print_trapframe、print_regs、
  print_ticks、trap_dispatch、trap。
- 不要实现 exception_handler（别人负责），也不要动 trapentry.S。
- 禁止在时钟中断路径里调用 sbi_shutdown()、panic() 或 while(1) 死循环——
  评分测试要等 ticks 连续涨到 200，提前关机/死机测试直接挂。

输出要求：直接给出可粘贴进 trap.c 的完整函数实现。完成后在 code/ 目录用 make
确认能编译通过。

[RELY]
// struct trapframe（kern/trap/trap.h）
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

// 中断号（libs/riscv.h 已定义，直接用宏，别手写数字）
#define IRQ_S_SOFT   1   // 软件中断
#define IRQ_S_TIMER  5   // 时钟中断
#define IRQ_S_EXT    9   // 外部中断

// 时钟（kern/driver/clock.h）
extern volatile size_t ticks;     // 每来一次时钟中断 +1
void clock_set_next_event(void);  // 重新武装下一次时钟中断，每次都必须调

// trap.c 里已有的现成函数，直接调用
void print_trapframe(struct trapframe *tf);  // 打印完整寄存器现场
static void print_ticks(void);               // 已实现，打印 "100 ticks\n"
#define TICK_NUM 100
// cprintf 在本文件已可用（print_ticks 就在用）。

// scause 编码约定：bit63=1 表示这是中断，低 bits 才是中断号。
// 注意：评分用例会直接以 tf->cause=15（不带 bit63）调用本函数做兜底测试，
// 所以提取中断号要统一屏蔽 bit63：
//     int irq = (int)(tf->cause & 0x7FFFFFFFFFFFFFFFULL);
// 这样：真实时钟中断 cause=0x8000000000000005 -> irq=5；软件中断 -> irq=1；未知 15 -> irq=15。

[GUARANTEE]
必须交付：
void interrupt_handler(struct trapframe *tf);

不允许：
- 不新增对外符号、不改任何现有函数签名。
- 不修改 print_ticks()（它内部的 #ifdef DEBUG_GRADE ... panic 不要动）。
- 不在时钟分支调用 sbi_shutdown / panic / while(1)。
- 不实现 exception_handler。

[SPECIFICATION]

## interrupt_handler(struct trapframe *tf)
Pre-Condition：本函数由 trap_dispatch 在"这是一个中断"时调用；tf 指向栈上保存的
trapframe，tf->cause 即 scause 值。

第一步：提取中断号
    int irq = (int)(tf->cause & 0x7FFFFFFFFFFFFFFFULL);
然后 switch(irq)。

### Case IRQ_S_TIMER (5) —— 时钟中断（核心）
Post-Condition：
1. 必须先调用 clock_set_next_event() 重新武装下一次时钟中断。
   不调则系统只响一次时钟中断，测试必挂。
2. ticks++。
3. 当 ticks % TICK_NUM == 0（每 100 次≈每秒），调用 print_ticks() 打印 "100 ticks"。
Requirements：本分支不关机、不 panic；ticks 必须能持续累加到 200 以上。

### Case IRQ_S_SOFT (1) —— 软件中断
Post-Condition：严格打印这一行：
    cprintf("Supervisor software interrupt\n");
不做其他动作。

### Case IRQ_S_EXT (9) —— 外部中断（本实验不展开）
打印一行提示即可，例如 cprintf("Supervisor external interrupt\n"); 不做其他处理。

### default —— 无法识别的中断
Post-Condition：
1. 严格打印这一行（irq=15 时输出必须是 "Unknown interrupt: cause 15"）：
    cprintf("Unknown interrupt: cause %d\n", irq);
2. 再调用 print_trapframe(tf) 打印完整现场辅助调试。

## 验收标准（评分脚本 grep 的输出串，必须全部出现）
- 时钟持续触发：连续出现多行 "100 ticks"，ticks 能涨到 200。
- 软件中断分支：出现 "Supervisor software interrupt"。
- 未知中断兜底：出现 "Unknown interrupt: cause 15"。
- 在 code/ 目录 make 编译通过。
```
