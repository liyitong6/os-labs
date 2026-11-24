# B 双重 GDB 现场记录与 QEMU 7.0.0 源码分析

本文件供 A 整理实验报告、B 准备答辩使用。事实来自用户上传的 8 张截图、`host.log`、`guest.log`、`qemu.log` 和本机 QEMU 源码；完整原始上传保存在 [evidence/raw/lab5_B_debug_records.tar.gz](evidence/raw/lab5_B_debug_records.tar.gz)。图片索引见 [evidence/README.md](evidence/README.md)。

## 环境与验证范围

| 项目 | 本次实际环境 |
|---|---|
| WSL | Ubuntu-22.04，用户 liyitong |
| QEMU | `/opt/qemu/bin/qemu-system-riscv64`，7.0.0，带调试符号 |
| 对应源码 | `/home/liyitong/qemu-7.0.0`；GDB 显示编译目录为其 `build` 子目录 |
| 宿主 GDB | Ubuntu 12.1，调试 x86-64 QEMU 程序 |
| 客体 GDB | riscv64-unknown-elf-gdb 16.3.90.20250610-git，调试 RISC-V 内核和用户程序 |
| 运行对象 | B 独立 fixture；真实 B 源码，A/C 的进程与内存服务为测试替代实现 |
| 运行结果 | `qemu.log` 含全部 11 个 PASS 检查点及 `B_QEMU_RESULT: PASS`；双重 GDB 完整轮次的 QEMU 进程 915 正常退出 |

宿主日志前部还包含一次较早的进程 873 正常退出。本文分析的是随后进程 915 的完整观测轮次。GDB 日志主要记录调试器输出，不能假设它同时保存了 QEMU 的全部终端输出；运行检查结果以 `qemu.log` 和现场图为依据。

本次没有进行 A/C 合并工程的完整构建、正式 ELF 装载/fork/wait/exit 验证或 `make grade`。当前地址属于这个 fixture 构建，复现时应重新反汇编确认。

## 一次 SYS_putc 的完整运行记录

| 阶段 | 实测状态 | 对应截图 |
|---|---|---|
| 用户 ECALL 前 | `pc=0x8004b2`，`sp=0x1001fd0`；`a0=30`，`a1=85`，其余参数槽为 0 | 02 |
| QEMU 异常处理入口 | `exception_index=8`，`priv=0`，`pc=0x8004b2` | 03 |
| QEMU 完成入陷 | `priv=1`，`sepc=0x8004b2`，`scause=8`，`pc=stvec=0x80201f80` | 04 |
| 内核 syscall 入口 | `current->tf->gpr.a0=30`，`gpr.a1=85`；`epc=0x8004b6` | 05 |
| 内核 SRET 前 | `pc=0x8020204e`；`a0=0`，`sp=0x1001fd0`，`sepc=0x8004b6`，`sstatus=0x200040020` | 06 |
| QEMU 执行 SRET | `priv: 1 → 0`；`mstatus: 0xa000400a0 → 0xa000400a2`；函数返回 `8389814=0x8004b6` | 07 |
| 实际返回用户态 | `pc=0x8004b6`，下一条为 `sext.w a0,a0`；`sp=0x1001fd0`，`a0=0` | 08 |

本实验的系统调用 ABI 将编号放在 **a0**、参数放在 **a1 至 a5**，返回值放回 **a0**。这里的 30 是 SYS_putc，85 是字符 U，0 是本次调用的成功返回值。不要套用其他系统使用 a7 存系统调用编号的约定。

QEMU 把发生异常时的 PC 保存到 `sepc`，因此图 04 中仍为 `0x8004b2`。随后本项目 `kern/trap/trap.c:131–134` 的 USER_ECALL 分支执行 `tf->epc += 4` 再调用 `syscall()`，所以图 05 的 trapframe 已变为 `0x8004b6`。ECALL 是 4 字节指令；COW 写缺页则要重试原写指令，不能沿用这个 PC 推进规则。

## ECALL：翻译与异常递交

以下 QEMU 文件和行号均以原始上传包中的 QEMU 7.0.0 源码为准。

1. `target/riscv/insn_trans/trans_privileged.c.inc:21–26` 的 `trans_ecall` 在翻译阶段调用 `generate_exception(ctx, RISCV_EXCP_U_ECALL)`，生成异常执行路径。
2. `target/riscv/translate.c:222–227` 的 `generate_exception` 生成保存当前来宾指令地址、调用异常 helper 的 TCG 操作，并标记本 TB 不再正常顺序执行。`ctx->base.pc_next` 在这里对应正在翻译的指令地址；本次实测也确认保存的是 ECALL 自身的 `0x8004b2`。
3. 生成的宿主代码运行时，`target/riscv/op_helper.c:27–38` 的 `helper_raise_exception` / `riscv_raise_exception` 设置 `exception_index`，退出当前执行路径。
4. `accel/tcg/cpu-exec.c:644–683` 的 `cpu_handle_exception` 通过目标的 `do_interrupt` 回调递交异常。本次真实栈为 `cpu_exec → cpu_handle_exception → riscv_cpu_do_interrupt`，见图 03 和宿主日志。
5. `target/riscv/cpu_helper.c:1327` 起的 `riscv_cpu_do_interrupt` 分离异步标志与 cause，按当前特权级修正 ECALL 原因，检查异常委托。走到 S 态分支时，将 SIE 保存到 SPIE、把原特权级保存到 SPP、清除 SIE，写入 `scause/sepc/stval`，把 PC 设为 `stvec` 的入口并切到 S 态。

`trans_ecall` 先统一生成 U_ECALL 原因，运行时处理函数再按实际特权级区分 U/S/M 等情况。本次原特权级为 U，最终 `scause=8`。这解释了为什么不能仅看翻译函数里的常量就认为所有 ECALL 都来自用户态。

## SRET：恢复特权级与返回 PC

`target/riscv/insn_trans/trans_privileged.c.inc:74–88` 中，`trans_sret` 检查 S 扩展，发出：

```c
gen_helper_sret(cpu_pc, cpu_env);
tcg_gen_exit_tb(NULL, 0);
ctx->base.is_jmp = DISAS_NORETURN;
```

这里 `cpu_pc` 是 helper 的输出。`translate.c:1212` 将它定义为与 `CPURISCVState.pc` 对应的 TCG 全局值。

`target/riscv/op_helper.c:119–177` 的 `helper_sret` 执行权限、对齐、TSR 等检查，取 `retpc=env->sepc`；从 SPP 得到返回特权级，按 SPIE 恢复 SIE、把 SPIE 设为 1、清除 SPP，最后调用 `riscv_cpu_set_mode` 并返回 `retpc`。源码还包含虚拟化分支；本次只证明现场走通的普通用户返回，不把全部分支视为均已测试。

现场的 `mstatus` 从 `0xa000400a0` 变为 `0xa000400a2`，差值为 `0x2`，即 SIE 从 0 恢复为 1；返回前 SPP 为 0，故 `priv` 从 S 切到 U。该字段变化描述的是状态位恢复，不能理解为 U 态因此获得了 S 态权限。

宿主 GDB 对 helper 执行 `finish` 后，停在 `code_gen_buffer`，返回值是 `8389814`。这与源码一致：helper 返回目标 PC，TCG 生成的代码继续接收输出并结束该 TB。此时宿主 `$pc` 是 **x86-64 QEMU 的执行地址**；用户态的最终 PC 要在客体 GDB 中确认。图 08 实测回到 `0x8004b6`，补齐了这一环。

## TCG、TB 缓存与另一个双重 GDB 实验的联系

TCG 将 RISC-V 指令转换为中间操作，再生成可在宿主 x86-64 上执行的代码。翻译函数 `trans_*` 负责生成操作，运行时 helper 负责执行相应语义；翻译函数并不是每执行一次来宾指令就重新调用一次。

上传的 `accel/tcg/cpu-exec.c:978–987` 先做 `tb_lookup`，找不到合适的 TB 才 `tb_gen_code`。`cpu_tb_exec` 在第 357 行通过 `tcg_qemu_tb_exec(env, tb_ptr)` 执行生成的宿主代码。TB 的查找还带有 PC、地址空间/CPU 状态和 flags，不能把 TB 简化成只按 PC 永远复用。

因此，若以后要单独命中翻译断点，应在首次翻译前设置或重新启动 QEMU；已经缓存的 TB 可以继续执行，运行时 helper 断点仍可能命中。这是源码能够支持的解释，**本次没有额外执行 TB 缓存命中率或翻译断点实验**。实际运行证据是异常回调命中，以及 SRET helper 返回 `code_gen_buffer`；翻译函数本身依据对应版本源码阅读。

与页表/TLB 的双重 GDB 实验相比，两者都需要区分模拟 CPU 的架构状态、宿主执行状态与缓存。TB 缓存保存翻译后的代码，TLB 缓存保存地址翻译相关信息，作用不同。先确认观测的是翻译、代码执行还是访存阶段，才能解释断点为什么命中或没有命中。

## 真实排查过程与 AI 协助记录

| 现场问题 | 实际原因或已确认范围 | 处理和结果 |
|---|---|---|
| guest.gdb 的 source 和日志路径报找不到文件，随后没有寄存器 | 本次 GDB 对这些命令中带双引号的文件名处理不符合生成器假设，命令文件在连接前中止 | 改用已验证的原生文件名写法，重新设置日志并连接 `localhost:1234` |
| 加载 gdb_helpers.py 报 `Undefined command` | Python 文件被当成 GDB 命令文件处理；未核实该 GDB 的 Python 编译支持 | 默认生成脚本移除 Python helper 依赖，改用已实际走通的 `thbreak/x/i/info registers` 等原生命令；不宣称已经证明 GDB 没有 Python |
| 一次粘贴多条查询发生语法错误 | 多行被当作同一条表达式，错误输出在 host.log 中保留 | 每次只复制并执行一条，看到新 `(gdb)` 再继续 |
| 第一次 sys_putc 断点后继续运行，连接关闭 | fixture 已运行到正常结束 | 从宿主 GDB 重新启动 QEMU，连接新一轮，再在实际 ECALL 地址设置临时硬件断点 |
| 宿主 Ctrl+C 后停在 ppoll，提示缺少系统库源码 | 暂停点在宿主系统库；QEMU 目标源码仍可读取 | 在目标函数设置断点，恢复宿主后让客体单步，随后成功命中 |
| `info line trans_sret` 指向 translate.c:114 | 这行是被内联的 `has_ext`；构建含 `-g -O2`，不能把首条映射行当作函数定义位置 | 对照实际 `trans_privileged.c.inc:74` 的定义，区分优化后的行号映射与源文件定义 |
| helper 返回到 code_gen_buffer | 返回位置属于 TCG 生成的宿主代码 | 对照 `gen_helper_sret(cpu_pc, cpu_env)`，再从客体确认最终返回 PC |

AI 协助包括解释两个 GDB 的控制顺序、定位脚本启动失败、根据实测寄存器校验 U/S 切换、核对源码与生成代码的关系、提前标明截图边界及整理材料。现场输出和错误均保留，没有将源码推导写成未发生的运行结果。

## 可直接用于答辩的简短说明

本次用两个 GDB 观察一次 SYS_putc。用户在 a0 放编号 30、a1 放字符 85，执行 ECALL；QEMU 将其作为 U 态异常递交到 S 态入口，并保存 sepc。内核保存现场，将返回地址增加 4，分发系统调用并把结果 0 写回 a0。恢复现场后执行 SRET，QEMU 根据 SPP/SPIE 恢复特权级和中断状态；最后在用户态 `0x8004b6` 继续执行。TCG 负责将这些来宾指令翻译为宿主代码，运行时 helper 执行特权状态变更，截图和日志覆盖了完整往返。
