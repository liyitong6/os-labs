# Lab5 B 更新包

本包包括 `lab5_B/` 和 `lab5_B_qemu_checks/` 两个目录。B 的 10 个实现文件以及 fixture 的 8 个运行源文件与此前通过用户 WSL 运行的交付版逐字节一致；本次更新调试脚本、回归检查、真实证据和材料说明。

## 已完成内容

- 用户 WSL Ubuntu-22.04：8 个 B 目标编译单元通过。
- QEMU 7.0.0：独立 B fixture 的 11 个运行检查点通过。
- 双重 GDB：完成一次 SYS_putc 的用户态 → 内核 → 用户态往返，8 张截图及原始日志已归档。
- 已读取用户本机对应 QEMU 源码，补齐 ECALL、SRET、TCG/TB 和排查过程说明。
- 两个调试脚本生成器默认使用原生 GDB 命令；B 生成器 4 项、fixture 生成器 2 项回归通过。回归检查生成内容，未将其冒充更新脚本在目标 GDB 中的再次执行。

## 在现有 WSL 目录中使用

继续使用已有的 `~/oslab/lab5_B_work` 工程。将 `lab5_B_updated.zip` 放到 `~/oslab/lab5_B_package`，在 Ubuntu-22.04 中：

```bash
cd ~/oslab/lab5_B_package
python3 -m zipfile -e lab5_B_updated.zip .
```

解压更新同名的资料和验证工具目录。现场原始记录保存在 `lab5_B/docs/evidence/raw/`，不依赖用户以后是否重新生成 fixture 的 `build` 目录。

## 交给 A 的材料

| 位置 | 内容 |
|---|---|
| `lab5_B/docs/B_materials.md` | B 的实现与接口说明、真实修复、验证范围 |
| `lab5_B/docs/GDB_OBSERVATIONS.md` | 已完成的现场记录、QEMU 7.0.0 源码与 TCG 分析、答辩简述 |
| `lab5_B/docs/evidence/README.md` | 8 张截图对应的内容和数值 |
| `lab5_B/docs/evidence/screenshots/` | 8 张用户原始截图 |
| `lab5_B/docs/evidence/raw/` | 原始日志及用户上传的源码记录压缩包 |
| `lab5_B/docs/evidence/validation.json` | 原始资料哈希与离线核对结果 |
| `lab5_B/docs/INTERFACES_A_C.md` | 与 A/C 的集成接口和边界 |

A 负责把这些材料写入整组报告；B 提供自己的实现、验证和调试材料。

## 仓库整理状态

本包已把截图、日志和说明放入 B 资料目录，尚未写入用户本机仓库，也未提交或推送。进入实际仓库前先检查状态：

```bash
git -C ~/oslab/os-labs status --short --branch
```

后续按仓库实际结构放置代码、资料和独立验证工具，保留这些材料之间的相对链接。合并实现时使用 B 安装器的按函数合入方式，遵守 `INTERFACES_A_C.md` 中的约定。

## 当前边界

fixture 使用真实 B 源码，但 A/C 的进程和内存服务采用测试替代实现。完整 Lab5 构建、A/C 真实实现的联调及官方评分待合并后执行。TCG 翻译函数依据对应源码阅读，实际运行命中的是异常回调和 SRET helper，没有声称进行过额外翻译阶段断点或缓存命中率实验。
