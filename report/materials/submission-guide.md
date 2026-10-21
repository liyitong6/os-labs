# 终稿同步与提交说明

整合版已在李易桐的 WSL 中重新评分并通过 20/20。本终稿内核源码保持该版本，只补齐原图、报告状态、图注及归档。下面先把终稿放入本地仓库，再检查并提交。

## 1. 下载终稿，解压到新目录

将 `lab2-final-20261021.zip` 下载到 Windows 的“下载”文件夹。在 Ubuntu 22.04 逐条执行：

```bash
mkdir -p ~/oslab/lab2-final-20261021
```

```bash
unzip -n /mnt/c/Users/lyt13/Downloads/lab2-final-20261021.zip -d ~/oslab/lab2-final-20261021
```

包顶层只有 `code/` 和 `report/`。如果浏览器给下载文件加了编号，使用实际最新文件名。

## 2. 自动备份并同步

```bash
python3 ~/oslab/lab2-final-20261021/report/materials/apply_to_repo.py --repo ~/oslab/os-labs
```

脚本核对分支和源码哈希，将当前材料备份到仓库外的 `~/oslab/lab2-backups/`，再应用终稿。它会保留当前 `report/images/`、`report/materials/` 中新增而本包没有的文件，包含 `report/materials/grade-final.log`。源码在打包后有额外变化时，会在修改前停止并报告文件名。

脚本保留 `.git`，不执行提交或推送。成功后会显示备份路径。本终稿代码与已经通过新评分的版本一致，本次同步完成后进入 Git 检查即可。

## 3. 检查目录和变更

```bash
cd ~/oslab/os-labs
```

```bash
git status
```

```bash
ls
```

应处于 `lab2` 分支，顶层实验材料为 `code/`、`report/`；原根目录分报告和 Prompt 已归档。评分图片在 `report/images/grade-success.png`，报告为 `report/report.md`，总 Prompt 为 `report/prompt.md`。

```bash
git diff --check -- code
```

```bash
git diff --stat
```

## 4. 暂存、检查并提交

核对 `git status` 显示的改动对应本次实验整理后，逐条执行：

```bash
git add -A -- code report report.md prompt.md
```

这会同时记录原根目录两份已跟踪 Markdown 的移除；它们的内容已归档。

```bash
git diff --cached --stat
```

```bash
git status
```

```bash
git commit -m "lab2: finalize report, tests, and GDB evidence"
```

```bash
git push origin lab2
```

每次确认命令正常结束后再继续。若有报错或暂存列表出现其他工作内容，将完整输出发回再处理。

## 5. 最后确认

```bash
git status
```

确认工作区状态和远端同步情况；在 GitHub 的 `lab2` 分支打开 `report/report.md`，检查评分图、Buddy/Best-Fit 图和 GDB 图片能否显示。

## 附：已完成的评分命令

下面保留此次复评分使用的命令，便于复现，不是要求在相同代码上再次运行：

```bash
cd ~/oslab/os-labs/code
set -o pipefail
make grade QEMU=qemu-system-riscv64 2>&1 | tee ../report/materials/grade-final.log
```

其实际结果和原图见报告第五节。
