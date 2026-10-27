# Lab3 实验报告：A 阶段记录

> 本文目前仅记录 A 的工作。整组报告仍须汇总中断处理、异常处理、全量评分和三人实际贡献，按课程模板定稿。当前没有用专项测试代替整组验收。

## 1. 本阶段范围

负责成员：李易桐，学号 2412933。

本阶段完成：

1. `kern/trap/trapentry.S` 的 `RESTORE_ALL`。
2. `kern/trap/trapentry.S` 的 `__alltraps` 待填部分。
3. 将 Lab2 的 `page_init()` 和 Best-Fit 实现迁入 Lab3 对应位置。
4. 衔接内存地址上限、启动页表和 QEMU 启动方式。
5. 准备并运行 A 的独立检查。

正式 `interrupt_handler()`、`exception_handler()` 和整组最终评分尚待其他模块完成。

## 2. 整体执行逻辑

内核启动后初始化设备树、控制台和物理内存管理，将 `stvec` 设置为 `__alltraps`。进入 Trap 时，处理器记录返回地址、原因和状态，汇编入口再通过 `SAVE_ALL` 将软件需要保留的现场保存到栈上的 Trap frame。

`__alltraps` 将当前栈指针作为参数放入 `a0`，调用 C 语言接口 `trap(struct trapframe *tf)`。C 层根据原因处理中断或异常，必要时更新 Trap frame 中的返回地址。C 函数返回后，执行流进入 `__trapret`，由 `RESTORE_ALL` 恢复现场，再执行 `sret`。

本次基础内核运行在 S 模式；`sret` 根据恢复后的 `sstatus.SPP` 决定返回特权级，不能将它解释为每次都返回 U 模式。本阶段没有实现用户栈与内核栈切换。

## 3. 核心实现与 spec

### 3.1 `RESTORE_ALL`

输入为当前 `sp` 指向的 Trap frame。要求恢复返回所需状态及可写通用寄存器，并与既有 `SAVE_ALL`、`struct trapframe` 的布局严格一致，不改变这些既有接口。

Trap frame 包含 32 个通用寄存器槽位和 4 个 CSR 记录，RV64 下共 288 字节。第 32、33 个槽位分别对应 `sstatus`、`sepc`，第 2 个槽位保存分配 Trap frame 之前的 `sp`。

实现先用临时寄存器读出并写回 `sstatus`、`sepc`，再恢复通用寄存器，最后恢复 `sp`。将 `sp` 放在最后，是因为其他加载仍需用当前 Trap frame 地址定位。`x0` 是常量零寄存器，不执行恢复；保存的异常原因和附加地址供诊断使用，不需要写回作为返回现场。

### 3.2 `__alltraps`

要求在 `SAVE_ALL` 之后将正确的 Trap frame 地址传给 `trap()`，且返回后当前 `sp` 仍能定位原 Trap frame。

实现使用 `mv a0, sp` 传参，再用 `jal trap` 调用 C 函数。调用前已保存原 `ra`，因此函数调用对 `ra` 的修改可在返回流程中恢复。C 函数返回后顺序进入 `__trapret`。

### 3.3 Lab2 衔接

从小组实际提交的 Lab2 代码迁入 `page_init()`，保留设备树检查、地址换算、页描述符容量检查、保留页初始化和可用页登记。迁移后的 Lab3 保留 `local_intr_save()` / `local_intr_restore()` 包装，不用 Lab2 的直接调用版本覆盖它们。

原 `page_init()` 使用 `PHYSICAL_MEMORY_END`，而 Lab3 头文件未提供该宏，因此补入原有上限 `0x88000000`。保留 Lab3 的原子位操作定义。

Best-Fit 仅迁入分配器实现及 manager 定义，Lab3 原有 `basic_check()`、`best_fit_check()` 函数体保持原样；默认启动管理器仍为课程框架中的 `default_pmm_manager`。

## 4. 启动兼容问题与处理

启动页表迁入 Lab2 已使用的低地址恒等映射，使启用 `satp` 后仍能继续取启动代码指令，同时保留 Lab3 对精确栈顶地址的设置。

在本次实际验证环境中，使用框架原来的 `-device loader` 参数时，OpenSBI 日志显示 `Domain0 Next Address` 为 `0x0`，内核未开始执行。使用 `-kernel bin/ucore.img` 后，入口为 `0x80200000`，内核初始化及 A 专项测试通过。

因此将 Makefile 的运行、调试、测试目标及 `tools/grade.sh` 的 QEMU 加载参数改为 `-kernel`。评分脚本的测试用例、断言、匹配条件、超时和分值未改。诊断日志保存于 `materials/A/assistant-loader-diagnosis.log`。

## 5. 验证结果与范围

编译工具：`riscv64-unknown-elf-gcc` 13.2.0；模拟器：QEMU 8.2.2；固件：OpenSBI 1.7。这里记录的是代码准备环境中的实际结果，组员本机环境与截图仍需补充。

执行方式：`python3 tools/check_module_a.py`。准备环境中额外通过参数指定了编译器与 QEMU 的路径。

已观察到：

```text
memory management: default_pmm_manager
check_alloc_page() succeeded!
[Test 1] Trap entry and breakpoint return: PASS (35/35)
MODULE A CHECK: PASS
```

完整实际日志为 `materials/A/assistant-a-check.log`。

专项脚本在临时副本中使用测试用异常处理函数，调用课程提供的寄存器上下文与断点返回探针；探针设置被测寄存器后触发断点，再比较恢复值。外围 shim 保存探针调用者的 callee-saved 寄存器，原探针文件未修改。该结果验证 A 的主要入口与恢复链路，不构成正式异常处理模块验收，也不是整组 `make grade` 总成绩。

本机后续步骤：执行同一专项脚本，确认输出后保存真实截图至 `images/`，再由总报告负责人补入引用。当前未放入虚构截图或队友尚未运行的测试结果。

## 6. A 相关思考与原理

`stvec` 入口要求四字节对齐，地址低两位因此可以用于模式编码。旧的直接入口地址低两位为 `00`，在新编码下仍对应 Direct 模式，维持直接入口语义的兼容。

Trap 现场保存与进程上下文切换都涉及寄存器状态管理，但本阶段的 Trap 返回不等于发生进程调度。当前实验验证的是一次陷入及返回，尚未实现完整进程切换、用户地址空间切换和多核同步。

## 7. 待整组补齐

- 正式时钟、其他中断与异常处理的实现及解释。
- 中断开关、`do { ... } while (0)` 思考题和其余理论分析的最终整合。
- 三人实际 prompt、AI 工具及界面显示的模型信息。
- 组员本机 A 专项测试截图、整组最终 `make grade` 结果及截图。
- 全部报告条目、代码学号注释、图片引用、目录和 Git 提交检查。

本次原始源码中未发现完整的 `lab3_switch_test()` 实现或独立 Challenge 待填任务；不将指南示例中的一行可选注释作为已实现功能。
