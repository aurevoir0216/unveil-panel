#!/usr/bin/env python3
"""Build one Mosaico distribution; never flash, erase or select a boot app."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PINNED_IDF = '7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe'


def run(command, cwd, env):
    subprocess.run(command, cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('variant', choices=['desktop', 'idea'])
    args = parser.parse_args()
    if sys.version_info < (3, 10):
        parser.error('Use Python 3.10+ from the exported ESP-IDF environment')
    repo = Path(__file__).resolve().parents[1]
    env = dict(os.environ)
    if not env.get('IDF_PATH') or not shutil.which('idf.py'):
        parser.error('Load the pinned ESP-IDF export.sh before building')
    revision = subprocess.check_output(['git', '-C', env['IDF_PATH'], 'rev-parse', 'HEAD'], text=True).strip()
    if revision != PINNED_IDF:
        parser.error('ESP-IDF differs from the pinned revision')
    if args.variant == 'desktop':
        project = repo / 'firmware/mosaico'
        run([sys.executable, 'tools/prepare.py', '--idf', env['IDF_PATH']], project, env)
        run(['idf.py', 'bmgr', '-c', './boards', '-b', 'esp_mosaico'], project, env)
        if not (project / 'sdkconfig').exists():
            shutil.copy2(project / 'sdkconfig.release', project / 'sdkconfig')
        run(['idf.py', '-B', 'build-desktop', '-DESP_MOSAICO_BUILD_PROFILE=profiles/desktop.cmake', 'build'], project, env)
    else:
        project = repo / 'firmware/mosaico-idea'
        run(['git', 'submodule', 'update', '--init', '--recursive', '--',
             'firmware/mosaico/third-party/esp-mosaico-utils'], repo, env)
        env['ESP_IRIS_PYTHON'] = sys.executable
        run(['idf.py', 'build'], project, env)
        run(['cmake', '--build', 'build', '--target', 'system-update-bundle'], project, env)
    print(json.dumps({'variant': args.variant, 'project': str(project), 'installed': False}))


if __name__ == '__main__':
    main()
