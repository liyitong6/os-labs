#!/usr/bin/env python3
"""lab2 2413646: build/run real QEMU cases; require specific pass/assert logs."""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT.parent / "report" / "materials" / "buddy-tests"
LOGS = REPORT / "buddy_test_logs"
CASES = {
    1: ("zero allocation", ["buddy_initialized && n > 0"]),
    2: ("null release", ["pages != NULL && base != NULL && npage >= nbase"]),
    3: ("double release", ["!PageReserved(block) && !PageProperty(block)",
                           "(block->flags & BUDDY_ALLOCATED_HEAD) != 0"]),
    4: ("interior release", ["(block->flags & BUDDY_ALLOCATED_HEAD) != 0"]),
    5: ("wrong block size", ["buddy_request_size(n) == block_pages"]),
    6: ("misaligned descriptor", ["byte_offset % sizeof(struct Page) == 0"]),
    7: ("upper descriptor bound", ["index < page_count"]),
    8: ("reserved page release", ["!PageReserved(block) && !PageProperty(block)"]),
    9: ("zero release", ["buddy_initialized && n > 0"]),
    10: ("oversized release", ["buddy_request_size(n) == block_pages"]),
    11: ("referenced head release", ["page_ref(block) == 0"]),
    12: ("lower descriptor bound", ["base_addr >= array_addr"]),
    13: ("duplicate registration", ["PageReserved(base + i)"]),
    14: ("zero registration", ["buddy_initialized && n > 0"]),
    15: ("registration upper bound", ["index < page_count"]),
}


def options(case):
    args = ["PMM=buddy", "BUDDY_TEST=" + ("0" if case is None else "1")]
    if case is not None:
        args.append("BUDDY_CASE=" + str(case))
    return args


def build(case, name):
    command = ["make", "-B", "-j2"] + options(case)
    (LOGS / (name + ".log")).write_text("NOT RUN: build pending.\n", encoding="utf-8")
    with (LOGS / (name + "-build.log")).open("w", encoding="utf-8") as output:
        output.write("COMMAND: " + " ".join(command) + "\n")
        output.flush()
        result = subprocess.run(command, cwd=ROOT, stdout=output,
                                stderr=subprocess.STDOUT, timeout=60)
    if result.returncode:
        raise RuntimeError("build failed; see " + name + "-build.log")


def boot(case, name):
    path = LOGS / (name + ".log")
    command = ["make"] + options(case) + ["qemu"]
    with path.open("w", encoding="utf-8") as output:
        output.write("COMMAND: " + " ".join(command) + "\n")
        output.flush()
        proc = subprocess.Popen(command, cwd=ROOT, stdout=output,
                                stderr=subprocess.STDOUT, start_new_session=True)
        ok, detail = False, "timeout without required evidence"
        deadline = time.monotonic() + 20
        try:
            while time.monotonic() < deadline:
                text = path.read_text(encoding="utf-8", errors="replace")
                if "[BUDDY UNEXPECTED RETURN]" in text:
                    detail = "invalid operation returned instead of asserting"
                    break
                if case in CASES:
                    marker = "[BUDDY EXPECT PANIC] case=" + str(case) + "\n"
                    normalized = text.replace("\r\n", "\n")
                    specific = any("assertion failed: " + expr in text
                                   for expr in CASES[case][1])
                    if marker in normalized and "kernel panic at kern/mm/buddy_pmm.c:" in text:
                        ok = specific
                        detail = "expected allocator assertion" if ok else "wrong allocator assertion"
                        break
                    if "kernel panic at " in text:
                        detail = "panic from the wrong source or before the intended operation"
                        break
                else:
                    if "kernel panic at " in text:
                        detail = "unexpected kernel panic"
                        break
                    required = ("PMM check skipped: no check callback installed."
                                if case is None else "[BUDDY] ALL PASS (7 groups)")
                    if required in text and "satp physical address:" in text:
                        if case == 0 and text.count("[BUDDY PASS]") != 7:
                            detail = "missing normal test groups"
                        else:
                            ok, detail = True, ("ordinary startup restored" if case is None
                                                else "all 7 normal groups passed")
                        break
                if proc.poll() is not None:
                    detail = "QEMU exited before required evidence"
                    break
                time.sleep(0.1)
        finally:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=3)
    return ok, detail


def write_report(results):
    lines = ["# Buddy System 测试结果", "",
             "执行命令：`python3 tools/run_buddy_tests.py`（WSL os，code 目录；先加载工具链环境）。", "",
             "正常用例检查真实物理页；负面用例分别启动，只接受对应分配器断言。", ""]
    for name, description, passed, detail in results:
        if name == "normal":
            lines += ["## 正常测试", ""]
        elif name in ("negative-01", "negative-06", "negative-11"):
            first = int(name[-2:])
            lines += ["", "## 断言用例 %d–%d" % (first, first + 4), ""]
        elif name == "ordinary":
            lines += ["", "## 普通镜像恢复", ""]
        lines.append("- **%s** %s：%s。[%s](buddy_test_logs/%s.log)" %
                     ("PASS" if passed else "FAIL", description, detail, name, name))
    lines += ["", "## 覆盖范围与有效性", "",
              "- 受控真实页区验证非规则起点、尾部、分次登记与保留间隔，按预期页集合核对，不只比较总数。",
              "- 固定请求的独立预期页数、地址范围与数据图案共同检查取整、计数、重叠和数据隔离。",
              "- 只归还一个受控 8 页区后验证级联合并、两种释放顺序和旧首页失效，避免其他空闲大块掩盖漏合并。",
              "- 隔页释放及相邻非伙伴场景验证碎片和伙伴条件；超大请求失败前后检查计数与已有数据。",
              "- 耗尽后全部归还及 32 轮混合操作核对基线；独立负面启动核对具体断言，不能以超时冒充拒绝成功。", "",
              "正常套件先登记 12 页边界样本，验证后补全其余可用页，不重置分配器。", "",
              "限制：未验证并发、任意内部页引用或映射生命周期；不代表原 make grade 通过。", ""]
    normal_log = (LOGS / "normal.log").read_text(encoding="utf-8", errors="replace")
    ordinary_log = (LOGS / "ordinary.log").read_text(encoding="utf-8", errors="replace")
    test_count = re.search(r"\[BUDDY\] full pool: (\d+) pages", normal_log)
    ordinary_count = re.search(r"Free physical pages: (\d+)", ordinary_log)
    if test_count and ordinary_count:
        lines += ["日志记录：测试镜像可用 %s 页，普通镜像可用 %s 页；差异来自测试专用静态数据和代码。" %
                  (test_count.group(1), ordinary_count.group(1)), ""]
    if (LOGS / "first-env-failure-build.log").exists():
        lines += ["首次调用未加载工具链 PATH，编译器未找到，尚未执行测试；后续改用 Bash 登录环境运行。"
                  "[原错误日志](buddy_test_logs/first-env-failure-build.log)。", ""]
    (REPORT / "report_buddy.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    LOGS.mkdir(parents=True, exist_ok=True)
    results = []
    jobs = [(0, "normal", "normal behavior suite")]
    jobs += [(case, "negative-%02d" % case, CASES[case][0]) for case in CASES]
    try:
        for case, name, description in jobs:
            try:
                build(case, name)
                ok, detail = boot(case, name)
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                ok, detail = False, str(error)
            results.append((name, description, ok, detail))
            print(("PASS" if ok else "FAIL") + " " + name + ": " + detail, flush=True)
    finally:
        try:
            build(None, "ordinary")
            ok, detail = boot(None, "ordinary")
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            ok, detail = False, str(error)
        results.append(("ordinary", "ordinary Buddy startup", ok, detail))
        print(("PASS" if ok else "FAIL") + " ordinary: " + detail, flush=True)
        write_report(results)
    return 0 if all(row[2] for row in results) else 1


if __name__ == "__main__":
    sys.exit(main())
