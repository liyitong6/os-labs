# Lab5 B 模块交付

本包对应最终分工中的 B：系统调用、异常与恢复、调度依赖、COW 写缺页和双重 GDB 准备。默认学号沿用此前记录中的 **2412933**，可用 `--student-id` 修改。

已完成 B 代码及独立验证。用户的 WSL Ubuntu-22.04 已通过 8 个 B 编译单元检查，以及使用 A/C 测试替代实现的 11 个独立 QEMU 运行检查点。双重 GDB 已完成一次 SYS_putc 的用户态 → 内核 → 用户态运行轨迹；8 张现场截图、原始日志和对应 QEMU 7.0.0 源码分析均已整理。**这不是已通过整组评分的完整 Lab5**：完整联调与评分仍需合入 A/C 的实际实现。现场截图和说明见 [docs/evidence/README.md](docs/evidence/README.md)。

## 在 WSL 中开始

解压交付 ZIP，进入能看到 `lab5_B` 文件夹的目录。新建一个独立目录：

```bash
python3 lab5_B/scripts/bootstrap.py --destination ~/oslab/lab5_B_work
python3 lab5_B/scripts/cross_check.py --project ~/oslab/lab5_B_work
```

第一条命令从包内的老师原始 `lab5.zip` 建立 Lab5，并安装 B。目标目录已经存在时会退出，保留原有内容。第二条命令需要本机已有 `riscv64-unknown-elf-gcc`，只编译 B 所涉及的翻译单元，不执行整组链接或评分。

如果已经准备好 Lab5 工程，则使用：

```bash
python3 lab5_B/scripts/install_b.py --project /你的/Lab5目录 --check
python3 lab5_B/scripts/install_b.py --project /你的/Lab5目录
```

安装器为被修改的已有文件保存备份，只替换 B 专属文件，并按函数更新 `proc.c` 中的 `cpu_idle/proc_run/do_yield/do_kill`。A/C 的函数、原框架标记及官方评分文件保持原样。发现 B 专属文件已有自定义改动时，安装器会在写入前退出，需要按差异合并；不要为了安装而删除队友代码。

提交时审阅代码差异；备份目录和本地生成的编译、调试产物无需一并提交到仓库。

## 内容与验证

| 路径 | 内容 |
|---|---|
| `overlay/` | B 专属文件及 COW 新文件 |
| `snippets/proc_B.c` | 需按函数合入 `proc.c` 的四个 B 函数 |
| `B_changes.patch` | 相对本次原始 Lab5 框架的差异，便于审阅 |
| `docs/INTERFACES_A_C.md` | 发给 A、C 的对接约定和框架缺口 |
| `docs/B_materials.md` | 提供给 A 的实现、spec、真实修复与验证材料 |
| `docs/GDB.md` | 双重 GDB 的本机执行步骤及现场记录要求 |
| `docs/GDB_OBSERVATIONS.md` | 已完成的真实 SYS_putc 往返、QEMU 7.0.0 源码与 TCG 分析、排查过程 |
| `docs/evidence/` | 用户 WSL 现场截图、结果说明和后续待记录节点；整理仓库时一并提交 |
| `docs/prompt_B.md` | 当前真实用户要求与技术约束说明 |
| `scripts/prepare_debug.py` | 根据实际 ELF 生成调试脚本；可扫描本机 QEMU 源码 |
| `tests/`、`logs/` | 可重复的主机验证、汇编语义模型、安装器测试与执行日志 |

在安装有主机 GCC/CC、Python 3 的环境中：

```bash
python3 lab5_B/tests/run_tests.py
python3 lab5_B/tests/run_tests.py --sanitize
```

验证覆盖真实 B C 源码的系统调用、指针范围、调度、kill、COW、异常及中断逻辑；平台寄存器、分配器和 A/C 后端使用测试替身。汇编检查解释实际源码中的指令，检查 U/S、嵌套 Trap、寄存器与栈恢复；它不是汇编器或 QEMU 执行。内存检查启用 AddressSanitizer 和 UndefinedBehaviorSanitizer；当前沙箱不支持 LeakSanitizer，因此该项关闭。

关键说明：Lab5 的时钟分支在返回用户态前请求调度，支持 `spin/waitkill` 等忙循环场景；默认首次启动采用 A 手工构造 trapframe 后进入 `__trapret`，没有自行增加课堂 EBREAK 方案的隐藏调用约定。

先看 `docs/INTERFACES_A_C.md`，让队友按同一 COW 位和函数约定实现。本轮截图、日志和材料需在整理仓库时一并加入；当前更新包未替用户执行本机仓库提交或推送。
