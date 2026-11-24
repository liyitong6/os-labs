# B 阶段实现与验证材料

这是供报告负责人 A 使用的技术材料，未冒充整份实验报告。

## 实现与 spec

| 接口 | 输入/前提 | 结果与边界 |
|---|---|---|
| `exception_handler` | 当前进程及 Trap frame 有效 | USER_ECALL 推进 4 字节并分发；合法 COW 重试原写；致命用户异常结束当前进程；内核意外异常诊断停机 |
| `syscall` 与九个 `sys_*` | 按 a0/a1..a5 提供编号和参数 | 检查分发表边界与空项；非法编号返回 `-E_INVAL`；结果按有符号 int 扩展回 frame.a0；指针范围先检查 |
| `RESTORE_ALL` | sp 指向布局正确的 288 字节 frame | 按 SPP 设置 sscratch，恢复状态/epc 和整数寄存器；最后恢复 sp；SRET 完成特权级及中断使能返回 |
| `schedule` | proc_list/current/idleproc 初始化完成 | 环绕选 RUNNABLE，无候选则 idle；增加 runs；保持调用前中断状态 |
| `proc_run/switch_to` | 目标可运行、页表及 context 有效 | 更新 current，写 Sv39 satp，同步 TLB，保存恢复 14 项；相同目标不切换 |
| `do_yield/do_kill` | 当前 PCB 或待查 PID | yield 留下调度请求；kill 设置退出标志并唤醒可中断等待；保护特殊线程 |
| `cow_resolve_write` | 合法、可写 VMA 中的有效 COW 用户页 | 最后一个引用直接恢复权限；否则复制页面替换映射；拒绝真正只读/非法页；失败清理新页 |
| `cow_prepare_write` | 用户写入范围 | 校验范围及各页，先解除 COW；不直接写用户字节；明确允许准备阶段部分页已私有化的失败语义 |

## 本轮确认并修复的问题

1. **固定读取五个可变参数。**原用户库 `sys_fork()` 等传参很少，但公共函数仍执行五次 `va_arg`，会读取不存在的参数。本版本改用五个显式、定宽的参数槽，未使用槽填 0。
2. **RV64 存储宽度。**原内联汇编 `sd a0, ret` 中 `ret` 是 4 字节 int，存储却是 8 字节。本版本用 a0 寄存器输出并在 C 中转换为 int，取消该越界写法。
3. **内联汇编约束。**用固定 a0..a5 寄存器变量和读写约束，声明 memory，使编译器知道边界上的输入输出及内存副作用。
4. **系统调用函数声明顺序。**原框架的分发表在待补的静态处理函数之前。本版本先定义函数再建表，避免未声明标识符。
5. **异常处理函数的链接属性。**官方注入代码以 extern 调用 `exception_handler`，本版本保留全局定义。
6. **Lab4 时钟迁移不足。**Lab4 记录中的时钟处理只计时。检查 `user/spin.c` 可见子进程会无限循环；Lab5 时钟因此设置重调度标志，并由返回用户态的路径调度。尚未把这一静态推导写成真实 QEMU 的失败/修复截图。
7. **GDB 命令文件路径引号。**用户在真实双重 GDB 准备中提供截图：生成脚本对 `source` 和 `set logging file` 的路径使用了不适合该命令解析方式的双引号，辅助脚本加载失败、日志打开报错，命令文件在 `target remote` 前停止。日志命令改用现场已验证的文件名写法，保留 `file/add-symbol-file` 各自适用的参数格式，并改用 `set logging enabled on`。普通 GDB 的 deprecated 提示本身不会阻止运行。现场已恢复连接并完成完整往返。
8. **默认调试流程依赖 Python helper。**随后 source Python 文件被当作 GDB 命令处理，辅助命令没有注册。本次未进一步确认 Python 编译支持，不能据此断定 GDB 没有 Python。更新后的两个生成器默认不加载 helper，直接使用现场走通的原生 GDB 命令；可选 Python 文件保留但不自动执行。

这些是依据实际代码确认的缺陷或适配点，没有编造 QEMU 运行现象。

## 已执行验证与边界

- 真实 B C 源码在模拟平台与 A/C 后端下运行：5434 项断言，包括 896 种调度组合。
- 同一主机检查在 AddressSanitizer/UndefinedBehaviorSanitizer 下通过。LeakSanitizer 受当前运行环境限制，关闭后执行；没有据此宣称通过了泄漏检测。
- 对实际汇编源码解释执行：400 种 U/S、嵌套 Trap 与中断状态组合，以及 100 种 switch_to 保存恢复组合。
- 安装器 7 项检查：只读预览、幂等、保留 A/C 与评分文件、已有 B 函数替换、冲突退出、学号替换范围、备份一致性。
- 调试准备工具 4 项检查：从 ELF 读取真正的 `.text` 地址、拒绝非 ELF64 输入、按实际本地源码检索候选符号，以及含空格路径下的原生脚本生成回归。fixture 另有 2 项调试脚本回归。
- 使用老师真实头文件，完成 B 涉及的 6 个 C 翻译单元的主机语法检查。

用户已在真实 WSL Ubuntu-22.04 环境完成 8 个 B 编译单元检查；独立 fixture 在 QEMU 7.0.0 上的 11 个运行检查点全部通过，包括真实 Trap、CSR、Sv39、U/S 切换和 COW 写缺页。本次双重 GDB 又记录了一次 SYS_putc 的完整往返，宿主进程正常退出。原始日志和截图见 docs/evidence。

生成环境没有执行目标编译或 QEMU，实际运行证据来自用户提供的 WSL 现场。独立 fixture 的 A/C 服务是替代实现，因此不验证 A/C 算法或正式工程全部流程；尚未执行合并工程的 `make grade`。

## 双重 GDB 状态

已完成实际会话，归档 8 张图、宿主/客体 GDB 日志和 QEMU 运行日志，并读取用户本机 QEMU 7.0.0 对应源码。完整状态值、ECALL/SRET/TCG 解释和真实排查过程见 [GDB_OBSERVATIONS.md](GDB_OBSERVATIONS.md)。翻译函数依据源码阅读，未声称实际命中过额外的翻译阶段断点。生成器使用原生命令，避免本次 Python helper 加载问题。

## 依据与原理联系

依据本轮《练习》的模块一、模块四及旧实验依赖要求，《中断处理》《第一次进入用户态》和《分支任务：gdb 调试系统调用以及返回》。具体接口以本次老师 `lab5.zip` 为准。

体现了系统调用 ABI、特权级边界、上下文保存恢复、调度请求、COW 共享与隔离、引用计数和地址翻译同步；本版本未实现多核调度/互斥、跨核 TLB 同步、通用文件系统装载或全部扩展寄存器现场。

补充核准资料：

- [GCC Extended Asm](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html)：输入输出操作数、读写约束与 memory。
- [QEMU Translator Internals](https://www.qemu.org/docs/master/devel/tcg.html)：TCG、TB 和翻译/执行阶段的区分。
