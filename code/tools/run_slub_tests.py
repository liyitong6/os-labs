#!/usr/bin/env python3
"""lab2 2413646: real QEMU runs; preserve each attempt and exact panic evidence."""
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT.parent / 'report' / 'materials' / 'originals' / 'slub'
LOGS = REPORT / 'slub_test_logs' / datetime.now(
    timezone(timedelta(hours=8))).strftime('run-%Y%m%d-%H%M%S')
SKIP_PMM = '--with-pmm-check' not in sys.argv[1:]
BIT = '(slab->allocated[index / 64] & mask) != 0'
CASES = {
    1: ('small double free', 'slub_pmm.c', BIT),
    2: ('small interior pointer', 'slub_pmm.c', 'offset % size == 0'),
    3: ('outside slot area', 'slub_pmm.c', 'released'),
    4: ('unregistered address', 'slub_pmm.c', 'released'),
    5: ('large interior pointer', 'slub_pmm.c', 'released'),
    6: ('large double free', 'slub_pmm.c', 'released'),
    7: ('reclaimed slab pointer', 'slub_pmm.c', 'released'),
    8: ('repeated initialization', 'kmalloc.c', '!kmem_initialized'),
    9: ('allocation before initialization', 'kmalloc.c', 'kmem_initialized'),
    10: ('free before initialization', 'kmalloc.c', 'kmem_initialized'),
    11: ('free while disabled', 'kmalloc.c', 'byte allocator disabled: cannot free a non-NULL pointer'),
    12: ('unallocated slot', 'slub_pmm.c', BIT),
}


def run(label, pmm, kmem, case, tests=1, buddy_tests=0, pmm_check=None, boot=True):
    if pmm_check is None:
        pmm_check = int(not SKIP_PMM)
    opts = [f'PMM={pmm}', f'KMEM={kmem}', f'SLUB_TEST={tests}',
            f'SLUB_CASE={case}', f'BUDDY_TEST={buddy_tests}', f'PMM_CHECK={pmm_check}']
    path = LOGS / (label + '.log')
    command = ['make', '-B', '-j2'] + opts
    try:
        with (LOGS / (label + '-build.log')).open('w') as output:
            output.write('COMMAND: ' + ' '.join(command) + '\n')
            output.flush()
            result = subprocess.run(command, cwd=ROOT, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=90)
        if result.returncode:
            return False, 'build failed'
        if not boot:
            path.write_text('Default image rebuilt; not booted, so no page self-check ran.\n')
            return True, 'default image rebuilt (not booted)'
        command = ['make'] + opts + ['qemu']
        with path.open('w') as output:
            output.write('COMMAND: ' + ' '.join(command) + '\n')
            output.flush()
            proc = subprocess.Popen(command, cwd=ROOT, stdout=output,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            ok, detail = False, 'missing required evidence before timeout'
            try:
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    text = path.read_text(errors='replace').replace('\r\n', '\n')
                    if '[SLUB UNEXPECTED RETURN]' in text:
                        detail = 'invalid operation returned'
                        break
                    if tests and case:
                        _, source, expression = CASES[case]
                        marker = f'[SLUB EXPECT PANIC] case={case}\n'
                        expected = expression if case == 11 else 'assertion failed: ' + expression
                        if 'kernel panic at ' in text:
                            ok = (marker in text and
                                  f'kernel panic at kern/mm/{source}:' in text and
                                  expected in text)
                            detail = 'expected assertion' if ok else 'wrong panic'
                            break
                    else:
                        if 'kernel panic at ' in text:
                            detail = 'unexpected panic'
                            break
                        marker = ('[SLUB] ALL PASS (6 groups)' if kmem else
                                  '[SLUB] DISABLED PASS') if tests else 'satp physical address:'
                        if marker in text:
                            ok = not tests or not kmem or text.count('[SLUB PASS]') == 6
                            if buddy_tests and pmm_check:
                                ok = ok and '[BUDDY] ALL PASS (7 groups)' in text
                            detail = 'verified completion' if ok else 'missing test groups'
                            break
                    if proc.poll() is not None:
                        detail = 'QEMU exited early'
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
        if not pmm_check and ('PMM check skipped: PMM_CHECK=0.' not in text or
                              'check_alloc_page() succeeded!' in text or
                              '[BUDDY] ALL PASS' in text):
            ok, detail = False, 'page self-check was not explicitly skipped'
        return ok, detail
    except (OSError, subprocess.TimeoutExpired) as error:
        return False, str(error)


def write_report(results):
    begin, end = '<!-- SLUB TEST RESULTS START -->', '<!-- SLUB TEST RESULTS END -->'
    passed = sum(ok for _, ok, _ in results)
    lines = [begin, '## 测试配置与结果', '',
             f'本轮共 {len(results)} 项运行结果，通过 {passed} 项，失败 {len(results) - passed} 项。', '',
             '在 WSL os 的 code 目录运行 `python3 tools/run_slub_tests.py`；'
             '默认只用框架默认的 Best-Fit 提供页面，运行 SLUB 用例并跳过底层自检；追加 `--with-pmm-check` 可开启同一底层的自检。', '',
             ('本轮使用 PMM_CHECK=0，日志核对底层自检确实跳过；默认镜像只重建，不启动底层自检。'
              if SKIP_PMM else '本轮仅使用默认 Best-Fit，开启其底层自检并运行 SLUB 用例。'), '',
             '每个预期断言单独启动 QEMU，并核对用例标识、源文件及具体断言；超时不算通过。', '']
    for offset in range(0, len(results), 5):
        lines += [f'### 运行记录 {offset + 1}–{min(offset + 5, len(results))}', '']
        for label, ok, detail in results[offset:offset + 5]:
            relative = (LOGS / (label + '.log')).relative_to(REPORT).as_posix()
            lines.append(f'- **{"PASS" if ok else "FAIL"}** {label}：{detail}。[日志]({relative})')
        lines.append('')
    lines += ['## 覆盖范围与有效性', '',
              '- 规格边界、2048/2049、3036、一页半及整两页请求检查大小分流；预期类别用独立常量表，填满页面后的最小槽位间距核对实际规格，不依赖私有容量函数。',
              '- 超过单页理论槽位上限的分配覆盖扩充；共享页、保留一个在用对象及最后释放分别验证复用、不能提前回收、必须归还整页。',
              '- 请求范围完整写入不同图案，交错释放和跨页大对象共同检查覆盖范围、重叠及数据破坏；16 轮混合操作后恢复基线，检查历史状态残留。',
              '- 用直接页申请耗尽真实页面，分别验证新 slab、大对象失败和既有空槽仍可分配；检查失败不改计数和已有数据，全部归还后再次申请。',
              '- 使用默认 Best-Fit 提供真实页面；关闭模式、初始化误用和独立断言覆盖框架接入。默认核对底层自检跳过标识，不重复运行其他页分配器；可用 --with-pmm-check 开启同一底层自检。', '',
              '## 有效性分析与限制', '',
              '这些场景围绕初始化、申请、释放、失败及重复使用的状态转换，检查范围合法、所有权互斥、数据保持和资源守恒。'
              '资源数量、实际地址和数据内容相互核对，可减少单一检查恰好通过的情况。'
              '测试覆盖主要行为类别，并非穷举所有大小、布局和操作序列；未验证并发、越界写入拦截或地址复用后的旧指针识别，也不代表 make grade 通过。', '',
              f'原始构建日志、QEMU 日志和结果 JSON 保存在 `{LOGS.relative_to(REPORT).as_posix()}/`，失败尝试不会覆盖。', end]
    path = REPORT / 'report_slub.md'
    original = path.read_text(encoding='utf-8') if path.exists() else ''
    block = '\n'.join(lines)
    if begin in original and end in original:
        updated = re.sub(re.escape(begin) + r'.*?' + re.escape(end),
                         lambda _: block, original, count=1, flags=re.S)
    else:
        updated = original + ('\n\n' if original else '# SLUB 实现与测试记录\n\n') + block + '\n'
    path.write_text(updated, encoding='utf-8')


def main():
    LOGS.mkdir(parents=True)
    results = []
    # lab2 2413646: one default page backend; labels identify SLUB tests.
    started = time.monotonic()
    jobs = [('slub-normal', 'default', 1, 0, 1, 0)]
    jobs += [(f'slub-negative-{case:02}', 'default', int(case != 11), case, 1, 0)
             for case in CASES]
    jobs.append(('slub-disabled', 'default', 0, 0, 1, 0))
    print('SLUB tests: PMM=default (Best-Fit), PMM_CHECK=' +
          str(int(not SKIP_PMM)), flush=True)
    try:
        for label, pmm, kmem, case, tests, buddy_tests in jobs:
            ok, detail = run(label, pmm, kmem, case, tests, buddy_tests)
            results.append((label, ok, detail))
            print(('PASS ' if ok else 'FAIL ') + label + ': ' + detail, flush=True)
    finally:
        ok, detail = run('slub-restore-default', 'default', 0, 0, 0, 0,
                         pmm_check=1, boot=not SKIP_PMM)
        results.append(('slub-restore-default', ok, detail))
        print(('PASS ' if ok else 'FAIL ') + 'slub-restore-default: ' + detail, flush=True)
        (LOGS / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        write_report(results)
        passed = sum(ok for _, ok, _ in results)
        print(f'SLUB summary: {passed}/{len(results)} passed, '
              f'{(time.monotonic() - started) / 60:.2f} minutes', flush=True)
    return 0 if all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
