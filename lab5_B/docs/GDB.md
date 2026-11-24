# 双重 GDB：本次现场与复现方法

用户已在 WSL Ubuntu-22.04 / QEMU 7.0.0 完成 B 独立 fixture 的双重 GDB 往返。实际记录、QEMU 源码分析及排查过程见 [GDB_OBSERVATIONS.md](GDB_OBSERVATIONS.md)，8 张截图见 [evidence/README.md](evidence/README.md)。A/C 合并工程仍需联调。

默认生成脚本现在使用原生 GDB 命令，不自动加载 Python helper。在交互提示符处，每次执行一条命令，看到新 `(gdb)` 再执行下一条；空回车可能重复上一条命令。命令文件可以由 GDB 自己逐行读取。

## 先完成可调试构建

1. 本机用 `scripts/cross_check.py` 验证 B 的目标编译。
2. 正式工程需合入 A/C、跑通 hello 和基本退出等待，再执行 `make build-hello`。独立 B fixture 可先用同级的 `lab5_B_qemu_checks/run.py` 构建和验证，其调试入口见该目录 README。
3. 准备带调试符号的 **qemu-system-riscv64 本机程序**、与该程序一致的源码和构建信息。宿主机 GDB 用于调 QEMU；RISC-V GDB 用于调来宾 uCore。

可在自己的 QEMU 源码中按该版本帮助核对构建选项，例如 `--target-list=riscv64-softmmu --enable-debug --disable-strip --disable-optimization`。先运行该源码的 `configure --help` 核对，不把这些选项当作适用于所有版本的保证。

生成脚本：

```bash
python3 lab5_B/scripts/prepare_debug.py \
  --project ~/oslab/lab5_B_work \
  --program hello \
  --qemu-bin /实际/QEMU构建目录/qemu-system-riscv64 \
  --qemu-source /实际/QEMU源码目录
```

生成器从实际用户 ELF 读取 `.text` 地址，生成 `guest.gdb`；提供 QEMU 路径时还生成 `host.gdb`。源码搜索结果只是候选位置，要在自己的版本中确认函数定义和调用关系。记录目录每次新建，保留 `session.json` 中的版本和源码提交信息。

## 第一次观测：ECALL 的执行与异常递交

终端一用宿主机 GDB 启动 QEMU：

```text
gdb -x /刚生成的目录/host.gdb
```

它作为 QEMU 的父进程启动调试，QEMU 的 `-S` 让来宾 CPU 初始暂停，`-s` 开放来宾调试端口。不要同时再运行另一个占用 1234 端口的 `make debug`。

终端二：

```text
riscv64-unknown-elf-gdb -x /刚生成的目录/guest.gdb
continue
x/12i $pc
thbreak *0x8004b2
continue
x/i $pc
info registers pc sp a0 a1 a2 a3 a4 a5
```

脚本先在用户 `sys_putc` 停下，再反汇编找 ECALL，使用该次构建的真实地址设置临时硬件断点。示例 `0x8004b2` 来自本次 fixture，不可直接套用于其他构建。记录 a0 的编号、a1..a5 参数、PC、SP 和可读取的 CSR。

回到宿主机 GDB，按 Ctrl+C 暂停 QEMU。`info break` 核对断点编号，启用本地版本已确认的异常处理断点，禁用暂时不用的返回断点，然后 `continue`。此时再让来宾 GDB `si`，观察宿主机何处命中，并用 `bt`、`info args`、`info locals`、`list` 逐步解释异常递交。

本次确认的入口是 `riscv_cpu_do_interrupt`，源文件 `target/riscv/cpu_helper.c:1327`，GDB 在第 1328 行停下。函数入口可用 `set $rv = &((RISCVCPU *)cs)->env` 保存状态指针，执行 `finish` 后比较 `priv/sepc/scause/pc/stvec/mstatus`。其他版本应先核对本机定义。

注意两个调试器的先后关系：宿主机已暂停时，来宾请求可能阻塞，必须先恢复宿主机；观察完本轮后禁用无关断点，避免后续重复打断。

## 第二次观测：SRET 返回用户态

在来宾已经进入异常处理后执行：

```text
thbreak kern/syscall/syscall.c:syscall
continue
p/d current->tf->gpr.a0
p/d current->tf->gpr.a1
p/x current->tf->epc
thbreak __lab5_sret
continue
x/i $pc
info registers pc sp a0 sepc sstatus
```

先在内核 syscall 处核对 trapframe，再于 B 的 `__lab5_sret` 标签停下。若有多次返回需要过滤，可给断点加条件 `if $sepc == 本轮返回地址`。此时寄存器已恢复，`sepc` 应指向 ECALL 后的指令，SSTATUS.SPP 决定返回模式。

在宿主再次 Ctrl+C，禁用暂时不用的异常入口断点，设置 `break helper_sret` 并恢复宿主；随后在客体单步 SRET。宿主命中后先 `set $ret_env = env`，比较 `finish` 前后的 `$ret_env->priv` 和 `mstatus`，并记录 `sepc` 与函数返回值。再次恢复宿主，让客体原单步完成，核对用户 PC、a0 和 SP。宿主暂停时客体的 `si` 会等待；不要因此反复发送单步或 continue。

## TCG 源码阅读与额外翻译断点

TCG 把来宾指令转换为可执行的宿主代码，并以 TB 组织、缓存翻译结果；翻译和已经生成的代码执行是不同阶段。可结合官方 [Translator Internals](https://www.qemu.org/docs/master/devel/tcg.html) 阅读。

在本机源码搜索 `trans_ecall`、`trans_sret` 等候选，确认定义后，另开一轮从较早阶段设置翻译断点，必要时重新启动 QEMU。不要因为已经缓存的 TB 没有再次翻译，就判断某条指令没有执行。记录翻译阶段生成何种处理、执行阶段在哪里递交异常或完成返回，并与此前页表遍历调试中“翻译、TLB 与实际访问”的观测时机比较。

本次已经读取用户上传的对应 QEMU 7.0.0 源码。翻译函数本身未单独命中断点；运行时异常处理和 SRET helper 已实际命中。源码分析、TB 查找与生成路径、`code_gen_buffer` 现场的联系见 GDB_OBSERVATIONS.md。

## 每次必须留下的真实材料

| 位置 | 记录 |
|---|---|
| 用户 ECALL 前 | 程序名、编号、参数、PC、SP、相关 CSR |
| QEMU 异常处理 | 实际版本、源码文件/函数、调用栈、关键状态更新 |
| 内核 syscall | trapframe 中的编号/参数、epc 更新及返回值 |
| SRET 前后 | sepc、SP、SPP/SPIE/SIE 与实际恢复位置 |
| TCG | 对应源码、运行时命中点、翻译与执行的区别；额外缓存实验若未执行则明确注明 |
| 问题排查 | 命令、真实报错、原因、解决过程和 AI 交互 |

`host.log`、`guest.log`、`qemu.log` 和 8 张现场图已归档到 docs/evidence。将材料交给 A；完整实验的评分需等 A/C 合并后执行。
