# Lab3 提示词记录：A 部分

使用工具：ChatGPT。
以下提示词根据本次实际需求与调试过程，按技术任务归纳整理。

## 1. 实现上下文恢复 RESTORE_ALL

请依据 Lab3 提供的 trapentry.S 和 struct trapframe，实现 RESTORE_ALL 宏。

要求：
1. 严格对应已有 SAVE_ALL 的保存顺序和栈帧布局。
2. 恢复 sstatus、sepc 以及需要恢复的通用寄存器。
3. 正确恢复陷入前的 sp，说明为什么必须最后恢复 sp。
4. 区分返回执行所需状态与仅用于诊断的信息。
5. 保持 SAVE_ALL、struct trapframe 和既有接口不变。
6. 在对应实验注释处填写学号 2412933，并解释实现原理。

## 2. 实现 Trap 入口 __alltraps

请补全 __alltraps 中调用 C 语言处理函数的部分。

要求：
1. 在 SAVE_ALL 保存现场后，将当前 Trap frame 地址按调用约定传入 a0。
2. 调用 trap(struct trapframe *tf)。
3. 确保 C 函数返回后进入 __trapret，恢复现场并执行 sret。
4. 解释 ra 的保存与恢复，以及汇编入口、C 处理函数和返回流程的关系。
5. 不改变 trap() 的签名，不实现其他成员负责的正式中断和异常处理函数。

## 3. 迁移 Lab2 依赖

请对照小组已完成的 Lab2 源码与 Lab3 原始框架，将 page_init() 和 Best-Fit 实现迁入对应待填位置。

要求：
1. 检查物理内存范围、地址换算、页描述符初始化和空闲页登记。
2. 保留 Lab3 新增的中断状态保存与恢复逻辑、原子操作及既有测试。
3. 核对物理内存上限和启动页表映射，处理框架接口差异。
4. 保留框架默认管理器设置，区分实际启动自检与 Best-Fit 专项验证。
5. 列出迁移位置、必要的兼容修改及其原因。

## 4. 排查内核启动问题

使用 QEMU 启动后，程序停留在 OpenSBI，未进入内核。请结合启动命令、内核加载地址和 Domain0 Next Address 定位原因。

要求：
1. 核对内核镜像的加载地址与执行入口。
2. 比较当前环境下 -device loader 和 -kernel 的实际启动结果。
3. 根据验证结果调整必要的运行、调试及评分启动参数。
4. 保持评分测试用例、断言、匹配条件和分值不变。
5. 保存诊断依据和实际运行日志。

## 5. 设计 A 模块独立验证

正式 interrupt_handler() 和 exception_handler() 尚未完成，请设计能够独立验证 A 模块的检查方法。

要求：
1. 在临时工程副本中使用测试用处理函数，不覆盖正式源码。
2. 使用课程提供的寄存器上下文与断点返回测试，检查 Trap 入口和现场恢复。
3. 核对测试探针的调用约定，必要时通过外围 shim 保护调用者现场。
4. 保留原测试探针和判定条件。
5. 输出真实检查结果与完整日志，明确专项验证覆盖范围及尚未验证的内容。

## 6. 修复本机工具与编译问题

本机运行专项检查时，依次出现以下问题，请根据实际输出定位并给出最小修复步骤：

1. 检查脚本找不到编译器或 QEMU：
   使用 /opt/qemu/bin/qemu-system-riscv64，以及
   $HOME/compiler-env/riscv-elf-toolchains/bin/riscv64-unknown-elf-
   指定实际工具路径。

2. make 反复提示文件修改时间位于未来：
   检查文件时间戳，校正后重新执行构建。

3. 汇编器报 unknown CSR `sbadaddr`：
   核对旧名称与 stval 的对应关系，在汇编文件顶部添加兼容定义，保留 SAVE_ALL 的原有代码和 Trap frame 布局。

修复后重新运行 A 专项检查，并根据实际输出判断结果。

## 7. 整理验证结果与报告材料

请根据本机实际输出更新 A 阶段报告。

已确认的环境与结果：
- Ubuntu 22.04 / WSL。
- riscv64-unknown-elf-gcc 15.1.0。
- QEMU 7.0.0。
- OpenSBI 1.0。
- check_alloc_page() succeeded!
- Trap entry and breakpoint return: PASS (35/35)。
- MODULE A CHECK: PASS。

要求：
1. 记录实际运行命令及 materials/A/local-a-check.log。
2. 补充工具路径、时间戳和 CSR 名称兼容问题的处理过程。
3. 说明 Trap frame、sp 恢复、sret 和 stvec 的相关原理。
4. 仅引用实际取得的截图和测试结果。
5. 明确 A 专项通过，正式中断、异常处理及整组评分仍需后续完成。
