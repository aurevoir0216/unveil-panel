#!/usr/bin/env python3
"""Name an already built Unveil Idea bundle for distribution."""
import argparse
import json
from pathlib import Path
import re
import shutil
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idea-bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9][0-9A-Za-z.-]*', args.version):
        parser.error('Use a version such as 0.2 or 0.2-rc1')

    try:
        with zipfile.ZipFile(args.idea_bundle) as bundle:
            if bundle.testzip() is not None:
                raise ValueError('Bundle CRC check failed')
            manifest = json.loads(bundle.read('manifest.json'))
            components = {entry['file'] for entry in manifest['components']}
            if (manifest['schema'] != 'esp-iris-system-update/0.2'
                    or components != {'partition-table.bin', 'ota_0.bin', 'unveil.bin'}
                    or set(bundle.namelist()) != components | {'manifest.json'}):
                raise ValueError('Expected a retained Vibe Mode Unveil Idea bundle')
    except (OSError, KeyError, TypeError, ValueError, zipfile.BadZipFile) as error:
        parser.error(str(error))

    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / f'unveil-mosaico-idea-v{args.version}.irisfw'
    shutil.copyfile(args.idea_bundle, target)
    print(target.resolve())


if __name__ == '__main__':
    main()
