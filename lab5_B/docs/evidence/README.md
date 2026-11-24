# Lab5 B 现场验证截图

这些截图由用户在自己的 WSL Ubuntu-22.04 中运行并上传，原图归档在 `screenshots/`。整理仓库时应连同本说明、后续日志和新增截图一起提交。当前尚未执行仓库提交或推送。

| 文件 | 记录内容 | 观察结果 |
|---|---|---|
| [01_B部分独立运行测试通过.png](screenshots/01_B部分独立运行测试通过.png) | 从已有 `qemu.log` 筛选的 B 独立运行测试结果 | 11 个检查点均为 PASS，最终 `B_QEMU_RESULT: PASS`；画面保留了 A/C 使用 fixture substitutes 的说明 |
| [02_用户态ECALL执行前.png](screenshots/02_用户态ECALL执行前.png) | 客体 RISC-V GDB 停在用户态 `ecall` 执行前 | `pc=0x8004b2`，`sp=0x1001fd0`；`a0=30`（SYS_putc），`a1=85`（字符 U），`a2` 至 `a5` 为 0 |
| [03_QEMU处理用户态ECALL.png](screenshots/03_QEMU处理用户态ECALL.png) | 宿主 GDB 停在 QEMU 的 `riscv_cpu_do_interrupt` 函数入口 | 调用栈包含异常处理函数；`exception_index=8`，`priv=0`，`pc=0x8004b2`，此时尚未在该函数中完成 U 到 S 的切换 |
| [04_QEMU进入S态.png](screenshots/04_QEMU进入S态.png) | 宿主 GDB 执行 `finish`，停在异常处理函数返回后 | `priv=1`，`sepc=0x8004b2`，`scause=0x8`；`pc=stvec=0x80201f80`；`mstatus=0xa000400a0`，已进入 S 态异常入口 |
| [05_内核系统调用分发.png](screenshots/05_内核系统调用分发.png) | 客体 GDB 在 `kern/syscall/syscall.c:86` 的内核 `syscall()` 入口读取当前 trapframe | `gpr.a0=30`，`gpr.a1=85`，`epc=0x8004b6`，确认用户参数已保存到内核 trapframe，返回地址已增加 4 |
| [06_系统调用返回与SRET前状态.png](screenshots/06_系统调用返回与SRET前状态.png) | 客体 GDB 在 `__lab5_sret`、即 `sret` 执行前查看恢复后的寄存器 | `pc=0x8020204e`，`sp=0x1001fd0`，`a0=0`，`sepc=0x8004b6`，`sstatus=0x200040020`；本次 SYS_putc 已返回成功，用户寄存器已恢复，尚未执行 sret |
| [07_QEMU执行SRET切回用户态.png](screenshots/07_QEMU执行SRET切回用户态.png) | 宿主 GDB 在 `helper_sret` 执行前后读取同一个 CPU 状态 | `priv: 1 -> 0`；`sepc=0x8004b6`；函数返回值 `8389814`（即 `0x8004b6`）；`mstatus: 0xa000400a0 -> 0xa000400a2`，SIE 从 0 恢复为 1；返回位置为 TCG 的 `code_gen_buffer` |
| [08_返回用户态.png](screenshots/08_返回用户态.png) | 宿主继续运行后，客体 GDB 的原单步完成，检查用户指令与寄存器 | `pc=0x8004b6`，当前指令为 `sext.w a0,a0`；`sp=0x1001fd0`，`a0=0`，确认实际回到 ECALL 后一条用户指令 |

## 验证范围

- 用户报告的模拟器版本：QEMU 7.0.0。
- 独立 fixture 使用已安装的真实 B 源码，但 A/C 的进程和内存服务采用测试替代实现；这些结果不等于完整 Lab5 的合并联调或官方评分。
- ECALL 使用本实验的调用约定：`a0` 存系统调用编号，`a1` 至 `a5` 存参数，返回值写回 `a0`。
- 第 02、03 张记录的是同一次用户态 ECALL 的两个观察点；第 03 张仍处于异常入口，不能单独证明已经进入内核或返回用户态。
- 第 04 张继续记录同一次 ECALL：QEMU 完成入陷处理后，特权级变为 S 态，PC 指向内核异常入口。此时 `sepc` 仍是 ECALL 指令地址；随后内核的 `exception_handler` 会将 trapframe 中的 `epc` 加 4，再进行系统调用分发。
- 第 05 张已记录内核分发入口：同一次系统调用的编号和参数与用户态一致，trapframe 的返回地址已变为 `0x8004b6`。此截图尚未证明调用返回值或 `sret` 已执行。

## 原始资料与核对结果

- `raw/host.log`：宿主 GDB 输出，含完整轮次的异常回调、SRET helper、code_gen_buffer 和进程正常退出。
- `raw/guest.log`：客体 GDB 输出，含用户 ECALL、内核 syscall、SRET 前状态和实际返回用户代码。
- `raw/qemu.log`：独立运行的完整输出，11 个检查点均通过。
- `raw/lab5_B_debug_records.tar.gz`：用户上传的原始打包文件，包含上述日志及 5 个对应 QEMU 源文件，原字节保留。
- `validation.json`：离线核对记录及原始资料 SHA-256；这是对用户证据的检查，不是生成环境再次启动 QEMU。
- [../GDB_OBSERVATIONS.md](../GDB_OBSERVATIONS.md)：对应版本源码解释、TCG/TB、实际排查过程和供 A/B 使用的说明。

本次 8 张截图和日志已收齐，fixture 在双重 GDB 观测后正常退出。翻译函数采用源码阅读，未额外命中翻译阶段断点。完整 Lab5 的合并联调和评分由 A/C 合入后继续；用户本机仓库尚未提交或推送这些材料。

原图来源和 SHA-256 保存在同目录 `screenshots.json`，供仓库归档时核对。
