#!/usr/bin/env python3
"""Apply the B overlay and merge only B's four functions into proc.c."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import re
import shutil
import sys

PACKAGE = Path(__file__).resolve().parents[1]
PROC_NAMES = ('cpu_idle', 'proc_run', 'do_yield', 'do_kill')


def mask_c(text):
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  text, flags=re.S)


def function_span(text, name):
    masked = mask_c(text)
    pattern = r'(?m)^[ \t]*(?:static\s+)?(?:inline\s+)?(?:void|int)\s+' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{'
    found = list(re.finditer(pattern, masked))
    if len(found) > 1:
        raise ValueError('Multiple definitions of ' + name)
    if not found:
        return None
    m = found[0]
    depth = 1
    for i in range(m.end(), len(masked)):
        if masked[i] == '{':
            depth += 1
        elif masked[i] == '}':
            depth -= 1
            if depth == 0:
                return m.start(), i + 1
    raise ValueError('Unclosed function ' + name)


def merge_proc(existing, snippet):
    result = existing
    missing = []
    for name in PROC_NAMES:
        source = function_span(snippet, name)
        if source is None:
            raise ValueError('B snippet lacks ' + name)
        body = snippet[source[0]:source[1]]
        target = function_span(result, name)
        if target:
            result = result[:target[0]] + body + result[target[1]:]
        else:
            missing.append(body)
    if missing:
        marker = '//lab4 请补充你在lab4中的代码'
        section = '\n/* BEGIN LAB5 B: 2412933 */\n' + '\n\n'.join(missing) + '\n/* END LAB5 B */\n'
        if result.count(marker) == 1:
            result = result.replace(marker, marker + section, 1)
        else:
            result += section
    return result


def digest(data):
    return hashlib.sha256(data).hexdigest()


def make_plan(project, student_id):
    manifest = json.loads((PACKAGE / 'baseline_hashes.json').read_text())
    planned = {}
    for source in sorted((PACKAGE / 'overlay').rglob('*')):
        if not source.is_file():
            continue
        relative = source.relative_to(PACKAGE / 'overlay').as_posix()
        dest = project / relative
        if dest.is_symlink():
            raise ValueError('Refusing a symlink destination: ' + relative)
        original = source.read_bytes()
        rendered = original.replace(b'2412933', student_id.encode())
        if dest.exists():
            existing = dest.read_bytes()
            allowed = {manifest.get(relative), digest(original), digest(rendered)}
            if digest(existing) not in allowed:
                raise ValueError('B-owned file has custom changes; merge it manually: ' + relative)
        planned[relative] = rendered
    proc = project / 'kern/process/proc.c'
    if not proc.is_file() or proc.is_symlink():
        raise ValueError('Expected a regular kern/process/proc.c')
    snippet = (PACKAGE / 'snippets/proc_B.c').read_text().replace('2412933', student_id)
    planned['kern/process/proc.c'] = merge_proc(proc.read_text(), snippet).replace(
        '/* BEGIN LAB5 B: 2412933 */', '/* BEGIN LAB5 B: ' + student_id + ' */').encode()
    return planned


def apply_plan(project, plan):
    changed = {name: data for name, data in plan.items()
               if not (project / name).is_file() or (project / name).read_bytes() != data}
    if not changed:
        return None
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup = project / 'lab5_B_backups' / stamp
    previous = {}
    for name in changed:
        path = project / name
        previous[name] = path.read_bytes() if path.exists() else None
        if path.exists():
            dest = backup / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
    try:
        for name, data in changed.items():
            path = project / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    except BaseException:
        for name, data in previous.items():
            path = project / name
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)
        raise
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True,
                        help='Lab5 root containing Makefile and kern/')
    parser.add_argument('--student-id', default='2412933')
    parser.add_argument('--check', action='store_true', help='Preview without changing files')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9]{6,12}', args.student_id):
        parser.error('student-id must contain 6 to 12 digits')
    project = args.project.expanduser().resolve()
    makefile = project / 'Makefile'
    if not makefile.is_file() or not re.search(r'(?m)^PROJ\s*:=\s*lab5\s*$', makefile.read_text()):
        parser.error('project must be the Lab5 root (PROJ := lab5)')
    try:
        plan = make_plan(project, args.student_id)
        for name, data in plan.items():
            target = project / name
            status = 'UNCHANGED' if target.exists() and target.read_bytes() == data else 'UPDATE'
            print(status, name)
        if args.check:
            print('Preview complete; no files changed.')
            return
        backup = apply_plan(project, plan)
        print('B overlay applied.' if backup else 'B overlay already applied.')
        if backup:
            print('Backup:', backup)
        print('A/C implementation areas and official grading files were preserved.')
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
