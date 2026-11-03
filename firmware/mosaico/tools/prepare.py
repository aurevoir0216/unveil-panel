#!/usr/bin/env python3
"""Restore pinned Mosaico dependencies and apply the current Unveil patches."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def run(args, cwd):
    return subprocess.run(args, cwd=cwd, check=True, text=True, capture_output=True)


def apply_patch(root, patch):
    command = ['git', 'apply', '--whitespace=nowarn']
    if subprocess.run(command + ['--reverse', '--check', str(patch)], cwd=root,
                      capture_output=True).returncode == 0:
        print('Already applied:', patch.name)
        return
    run(command + ['--check', str(patch)], root)
    run(command + [str(patch)], root)
    print('Applied:', patch.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', default=os.environ.get('IDF_PATH'))
    args = parser.parse_args()
    platform = Path(__file__).resolve().parents[1]
    repo = platform.parents[1]
    spec = json.loads((platform / 'snapshot.json').read_text())
    idf = Path(args.idf).resolve() if args.idf else None
    if idf is None:
        parser.error('Set IDF_PATH or supply --idf /path/to/esp-idf')
    current = run(['git', 'rev-parse', 'HEAD'], idf).stdout.strip()
    if current != spec['upstream']['esp-idf']['commit']:
        parser.error('ESP-IDF commit differs from snapshot.json; use the pinned checkout')
    paths = ['firmware/mosaico/third-party/esp-claw',
             'firmware/mosaico/third-party/esp-mosaico-utils']
    run(['git', 'submodule', 'update', '--init', '--recursive', '--', *paths], repo)
    for name in ('esp-claw', 'esp-mosaico-utils'):
        root = platform / 'third-party' / name
        current = run(['git', 'rev-parse', 'HEAD'], root).stdout.strip()
        if current != spec['upstream'][name]['commit']:
            raise SystemExit('Dependency commit differs: ' + name)
    apply_patch(platform / 'third-party/esp-claw', platform / 'patches/esp-claw.patch')
    apply_patch(idf, platform / 'patches/esp-idf.patch')
    print('Dependencies prepared. Continue with board generation and idf.py build.')


if __name__ == '__main__':
    main()
