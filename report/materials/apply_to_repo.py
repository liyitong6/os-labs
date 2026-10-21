#!/usr/bin/env python3
"""Apply this reviewed Lab2 bundle to a matching local lab2 Git checkout.

Creates an external backup first. Does not stage, commit, or push.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def apply(repo, bundle):
    repo = repo.expanduser().resolve()
    bundle = bundle.resolve()
    if repo == bundle:
        raise RuntimeError('请从独立解压目录运行，目标应为原来的 Git 仓库。')
    if not (repo / 'code').is_dir():
        raise RuntimeError('目标缺少 code/，未修改。')
    branch = subprocess.check_output(['git', 'branch', '--show-current'], cwd=repo, text=True).strip()
    top = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], cwd=repo, text=True).strip()
    if branch != 'lab2' or Path(top).resolve() != repo:
        raise RuntimeError('目标必须是 Git 仓库根目录，且当前分支必须为 lab2；未修改。')
    manifest = json.loads((bundle / 'report/materials/integration-manifest.json').read_text())
    for rel, expected in manifest['code_after'].items():
        if digest(bundle / rel) != expected:
            raise RuntimeError('解压包源码与清单不符：' + rel)
    conflicts = []
    for rel, new_hash in manifest['code_after'].items():
        p = repo / rel
        if p.is_symlink() or (p.exists() and (not p.is_file() or digest(p) not in {manifest['code_before'][rel], new_hash})):
            conflicts.append(rel)
    for rel, expected in manifest['original_root_documents'].items():
        p = repo / rel
        if p.is_symlink() or (p.exists() and (not p.is_file() or digest(p) != expected)):
            conflicts.append(rel)
    if conflicts:
        raise RuntimeError('这些文件在打包后有额外变化，未修改，请保留输出：\n' + '\n'.join(conflicts))
    report = repo / 'report'
    if report.is_symlink() or (report.exists() and not report.is_dir()):
        raise RuntimeError('report/ 不是普通目录，未修改。')
    installed = report / 'materials/integration-manifest.json'
    if installed.is_file():
        old = json.loads(installed.read_text())
        complete = all((repo / p).is_file() and digest(repo / p) == h for p, h in manifest['code_after'].items())
        if old.get('bundle_id') == manifest['bundle_id'] and complete and not any((repo / p).exists() for p in manifest['original_root_documents']):
            print('同一整合版已应用；保留后续添加的日志、图片和报告修改。')
            return

    backup = repo.parent / 'lab2-backups' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True, exist_ok=False)
    shutil.copytree(repo / 'code', backup / 'code')
    incoming = backup / 'report-incoming'
    shutil.copytree(bundle / 'report', incoming)
    # Keep locally generated evidence that is not part of the downloaded bundle.
    carried = []
    for folder in ('images', 'materials'):
        evidence = report / folder
        if not evidence.is_dir() or evidence.is_symlink():
            continue
        for p in sorted(evidence.rglob('*')):
            if not p.is_file() or p.is_symlink():
                continue
            rel = p.relative_to(report)
            target = incoming / rel
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, target)
                carried.append(rel.as_posix())
    new_files = [p for p in manifest['code_after'] if not (repo / p).exists()]
    report_installed = False
    try:
        shutil.copytree(bundle / 'code', repo / 'code', dirs_exist_ok=True)
        if report.exists():
            shutil.move(str(report), str(backup / 'report'))
        shutil.move(str(incoming), str(report))
        report_installed = True
        for name in manifest['original_root_documents']:
            p = repo / name
            if p.exists():
                shutil.move(str(p), str(backup / ('root-' + name)))
    except Exception:
        shutil.copytree(backup / 'code', repo / 'code', dirs_exist_ok=True)
        for name in new_files:
            (repo / name).unlink(missing_ok=True)
        if report_installed and report.exists():
            shutil.move(str(report), str(backup / 'report-incomplete'))
        if (backup / 'report').exists():
            shutil.move(str(backup / 'report'), str(report))
        for name in manifest['original_root_documents']:
            p = backup / ('root-' + name)
            if p.exists():
                shutil.move(str(p), str(repo / name))
        raise
    print('整合版已应用到：' + str(repo))
    print('原材料备份：' + str(backup))
    if carried:
        print('保留本地新增证据：' + ', '.join(carried))
    print('尚未执行 Git 提交或推送。')
    print('此终稿内核源码与已通过 WSL 20/20 的版本一致；请检查 Git 状态后提交。')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    args = parser.parse_args()
    try:
        apply(args.repo, Path(__file__).resolve().parents[2])
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
