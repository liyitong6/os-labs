# Lab3 提示词记录：A 阶段

本文件由实际对话记录和明确标注的复现提示词组成。队友后续应继续追加自己的实际提示词及修复过程。

## 1. 实际使用方式

本阶段采用连续对话：用户上传指南和源码，确认选择 A，再提供 WSL、Git 状态及 Lab2 已提交源码。模型根据这些上下文完成迁移、实现和验证，没有额外声称运行过某条独立的结构化代码生成 prompt。

AI 工具为当前 ChatGPT 对话；模型名称和版本由成员依据实际界面补充，不沿用其他实验中的历史记录。

## 2. 原始用户提示词（按顺序）

1. 原始任务与提交要求：

   > 第一个文档是上次实验，这次该lab3了，我会把指南发给你，等我说发完了你再对我们组员三人进行分工。注意，指南中所有任务不可遗漏，都必须完成。现在统一说明一下实验交付物、提交方式以及git仓库的要求：
   > （1）git仓库：每个小组只需要准备一个git仓库，并将仓库设置为公开。我们需要统计每个小组的仓库信息，请各位组长填写问卷（只需组长填写，避免同组组员多次填写问卷），今天之内完成，如有特殊情况通过飞书联系。问卷只能提交一次，但可在提交记录中进行修改。问卷链接：https://nankai.feishu.cn/share/base/form/shrcnGKLY5Pmpd8G7TXxMvhhutg
   > （2）实验交付物和提交方式：源代码和实验报告均需上传到自己小组的git仓库中，一个分支对应一次实验，并以“labx”命名（例如：lab1），每一个分支中上传两个文件夹，分别以“code”和“report”命名。code文件夹包含对应实验的实现后的代码，report文件夹中包含实验报告（markdown文件，以”report.md“命名，具体格式参考下面的实验报告模板.md）、提示词（markdown文件，以“prompt.md”命名，汇总本实验所有的prompt）以及测试截图文件夹（以“images“命名，包含实验报告中提及的测试结果的截图）。[liyitong6/os-labs: Operating Systems Labs 2026](https://github.com/liyitong6/os-labs)  这是我们仓库

2. “发完了，记住分工三人工作量应该差不多”
3. “先不要规定abc都是谁，然后这个报告我要说一下，就是汇总三个人做的和全部实验报告，任务量也不能说很少”
4. “但是只负责整合会不会太轻松了呢？？”
5. “那个既不用等到最后再做，答辩的时候任务又会轻松一点？”
6. “ok我们做a，现在可以开始？”
7. “怎么打开？我记得我的版本是22.04”
8. 用户提供的实际终端记录：

   ```text
   liyitong@LiYiTong:/mnt/c/Users/lyt13$ cd ~/oslab/os-labs && git status --short --branch && git branch -a
   ## lab2...origin/lab2
     lab1
   * lab2
     remotes/origin/lab1
     remotes/origin/lab2
   ```

9. 用户提供的分支创建记录：

   ```text
   liyitong@LiYiTong:~/oslab/os-labs$ git switch -c lab3
   git status --short --branch
   Switched to a new branch 'lab3'
   ## lab3
   ```

10. “好了”（完成 Lab2 代码导出后）。
11. 用户上传 `lab2-code-for-lab3.zip`，未附加文字指令。

源码生成和验证所依据的资料：本次课程 `lab3.zip`、当前指南、实际 Lab2 代码导出包，以及前述对话。后续在本机运行时产生的新提示词应继续原样追加。

## 3. 整理后的复现提示词

> 以下内容整理于实现完成后，用于说明实现约束和供复现使用，不冒充已经逐字发送过的历史提示词。

```text
我负责 riscv64-ucore Lab3 的 A 部分，学号 2412933。
请依据本次提供的 Lab3 源码和小组已完成的 Lab2 源码：

1. 在 trapentry.S 中完成 RESTORE_ALL 和 __alltraps 的待填部分。
2. RESTORE_ALL 必须对应已有 SAVE_ALL 和 trapframe 布局，特别正确处理原 sp、sstatus 和 sepc。
3. 按 RISC-V 调用约定将 trapframe 地址传给 trap()，返回后通过 __trapret 和 sret 恢复原执行流。
4. 不修改 SAVE_ALL、trapframe 布局和 trap() 签名，不实现队友负责的正式 interrupt_handler/exception_handler。
5. 将 Lab2 的 page_init 和 Best-Fit 实现迁入对应位置，保留 Lab3 的中断保护和原子操作，以及框架已有的分配器测试。
6. 核查启动页表、物理内存上限和 QEMU 加载方式，以实际编译、运行结果定位问题。
7. 对 A 做独立验证，测试用处理函数只能存在于临时验证副本，不得替代队友的正式代码。
8. 提供修改说明、实际日志和安装步骤；没有运行过的测试、没有取得的截图不得写成已完成。
```

## 4. 后续追加位置

待追加：本机安装与验证对话、实际出现的问题及修复提示词、队友模块提示词、整组合并与最终评分记录。
