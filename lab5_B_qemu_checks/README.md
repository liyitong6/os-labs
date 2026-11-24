# B 独立 QEMU 运行夹具

本夹具从你当前的 `~/oslab/lab5_B_work` 读取并编译 B 的真实源码，在独立目录构建 ELF、启动 QEMU。不会修改 Lab5 工程，也不要求先完成 A/C。

用户已提供的前置结果：8 个 B 翻译单元通过真实 RISC-V 编译；主机专项和汇编语义模型通过；Ubuntu 22.04 上 QEMU 版本为 7.0.0，RISC-V GDB 为 16.3.90.20250610-git。

**用户已在 WSL Ubuntu-22.04 / QEMU 7.0.0 完成真实构建和运行，11 个检查点全部通过，并完成一次 SYS_putc 的双重 GDB 往返。**证据见同级 `lab5_B/docs/evidence`，源码分析见 `lab5_B/docs/GDB_OBSERVATIONS.md`。生成环境未执行目标编译或 QEMU；运行结论来自用户上传的现场日志和截图。

## 使用

把此 ZIP 放到 `~/oslab/lab5_B_package`，与原来的 `lab5_B` 文件夹同级，然后：

```bash
python3 -m zipfile -e lab5_B_qemu_checks.zip .
python3 lab5_B_qemu_checks/run.py --project ~/oslab/lab5_B_work
```

程序默认使用 `riscv64-unknown-elf-gcc`、对应 objcopy 和 `qemu-system-riscv64`。构建完成后，正常运行最多等待 45 秒，保存 `build/build.log`、`build/qemu.log` 和 `build/status.json`。

只有 QEMU 正常退出、11 个检查点全部出现、最终 PASS 标志存在且没有 FAIL 标志时，运行器才报告成功。超时或失败要保留完整输出，不要仅凭 QEMU 退出码判断。

## 检查什么

| 检查点 | 真实运行的 B 路径 |
|---|---|
| S_IRQ_STACK | S 态软件中断、SAVE_ALL/RESTORE_ALL、sscratch 和栈恢复 |
| CONTEXT_SWITCH | 两个手工 PCB 的 proc_run、Sv39 satp、真实 switch_to 和各自调用栈 |
| USER_ECALL_ABI | U 态 ECALL、实际用户库、系统调用分发及寄存器恢复；A/C 后端只校验转发参数 |
| COW_SHARED_WRITE | U 态对共享只读 COW 页实际写入、硬件写缺页、复制与重试，父映射保持原内容 |
| COW_LAST_REFERENCE | 引用仅剩 1 时原页恢复可写，不复制 |
| COW_NESTED_KERNEL_STORE | 系统调用内核写用户 COW 页，引发实际嵌套 S 态写缺页，返回后继续外层调用 |
| COW_PREPARED_KERNEL_STORE | 内核先 cow_prepare_write，再写用户页，保持共享方内容 |
| TIMER_PREEMPT_AND_KILL | 时钟请求调度、真实上下文切换、kill 唤醒等待线程；用户包含不主动让出的忙循环 |
| READONLY_REJECT | 真正只读用户页写入被拒绝，不可误当 COW |
| KERNEL_ACCESS_REJECT | U 态读取 supervisor-only 内核映射被拒绝 |
| COW_OOM_PRESERVE | 共享 COW 页分配失败时退出，旧映射、内容和引用保持完整 |

## 范围

真实 B 文件包括 trap.c、trapentry.S、内核 syscall.c、sched.c、switch.S、cow.c 以及用户 syscall.c。`proc.c` 中只抽取当前安装版本的四个 B 函数，运行器记录这些源文件 SHA-256。

PCB、初始地址空间、物理页池、页表辅助函数、用户映像映射以及 `do_fork/do_wait/do_execve/do_exit` 后端由夹具提供；它们不是 A/C 已完成的实现。用户程序使用固定测试映像，夹具不验证 A 的 ELF 装载、正式 fork、完整等待退出回收或 C 的分配器。

夹具内核为独立的 S 态测试内核，使用内核物理地址恒等映射和单 hart QEMU virt/OpenSBI 环境；正式 Lab5 的高地址映射和完整工程仍需联调。真实运行通过也不代表 `make grade` 或整组实验通过。

## 之后用于调试

只有构建并运行验证后，再考虑：

```bash
python3 lab5_B_qemu_checks/run.py --project ~/oslab/lab5_B_work --debug
```

此时 QEMU 停在启动位置。另开一个 PowerShell，以 `wsl -d Ubuntu-22.04` 进入同一发行版，执行运行器打印的 GDB 命令。默认脚本直接使用原生 GDB，不加载 Python helper；在 sys_putc 停下后用 `x/12i $pc` 查找真实 ECALL 地址并设置 `thbreak`。完整双重 GDB 的两个窗口控制顺序见 `lab5_B/docs/GDB.md`。夹具可用于 B 机制观察，完整工程仍需合入 A/C 后复核。

调试脚本生成回归可运行 `python3 lab5_B_qemu_checks/tests/test_debug_script.py`；该检查验证配置生成，不等于再次执行目标 GDB。

平台接口参考：

- [QEMU virt 平台与 OpenSBI 启动](https://www.qemu.org/docs/master/system/riscv/virt.html)。
- [RISC-V Supervisor ISA](https://docs.riscv.org/reference/isa/priv/supervisor.html)。
- SBI 调用采用本次老师框架 `libs/sbi.h` 中已有的接口。
