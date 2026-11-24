#!/usr/bin/env python3
"""Create a fresh Lab5 skeleton with B applied; never replace an existing folder."""
from pathlib import Path, PurePosixPath
import argparse
import os
import subprocess
import sys
import tempfile
import zipfile

PACKAGE=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--destination',type=Path,required=True)
    ap.add_argument('--student-id',default='2412933')
    args=ap.parse_args()
    destination=args.destination.expanduser().absolute()
    if destination.exists():ap.exit(1,'Destination already exists; no files changed.\n')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lab5-B-stage-',dir=destination.parent) as tmp:
        staging=Path(tmp)
        with zipfile.ZipFile(PACKAGE/'base/lab5.zip') as archive:
            for info in archive.infolist():
                rel=PurePosixPath(info.filename)
                if rel.is_absolute() or '..' in rel.parts or not rel.parts or rel.parts[0]!='lab5_new':
                    raise ValueError('Unexpected skeleton archive member')
                path=staging.joinpath(*rel.parts)
                if info.is_dir():path.mkdir(parents=True,exist_ok=True)
                else:
                    path.parent.mkdir(parents=True,exist_ok=True)
                    path.write_bytes(archive.read(info))
        project=staging/'lab5_new'
        subprocess.run([sys.executable,str(PACKAGE/'scripts/install_b.py'),
                        '--project',str(project),'--student-id',args.student_id],check=True)
        if destination.exists():raise FileExistsError('Destination appeared during preparation')
        os.rename(project,destination)
    print('Created:',destination)
    print('This is a B-stage tree. A/C functions and dependencies still need merging.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,subprocess.CalledProcessError) as e:sys.exit(str(e))
