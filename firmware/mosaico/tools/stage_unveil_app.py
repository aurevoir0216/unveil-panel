#!/usr/bin/env python3
"""Seed desktop app code/resources in the system image; never seed user settings."""
import argparse
from pathlib import Path
import shutil
import hashlib
import struct
import zlib

ASSETS = ('Doto-Regular.ttf', 'Doto-Metrics.ttf', 'Unveil-Text-Readable.ttf',
          'Doto-OFL.txt', 'Noto-OFL.txt', 'icon.jpg')
UNUSED_LEGACY = {'three_geometry.lua', 'sector_points.lua', 'sector_specs.lua',
                 'dial_rows.lua', 'dial_inputs.lua', 'test_mode.lua'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path)
    destination.add_argument('--archive', type=Path)
    args = parser.parse_args()
    source = args.app.resolve()
    files = [*[p.relative_to(source) for p in sorted((source / 'scripts').glob('*.lua')) if p.name not in UNUSED_LEGACY],
             *[Path('assets') / name for name in ASSETS], Path('launcher.json')]
    for name in files:
        path = source / name
        if not path.is_file() or path.is_symlink():
            raise SystemExit('Missing or linked app resource: ' + str(name))
    if args.archive:
        entries = bytearray()
        for name in files:
            path = name.as_posix().encode('utf-8')
            data = (source / name).read_bytes()
            entries += struct.pack('<HII', len(path), len(data), zlib.crc32(data)) + path + data
        args.archive.parent.mkdir(parents=True, exist_ok=True)
        args.archive.write_bytes(b'UVSEED01' + hashlib.sha256(entries).digest() + struct.pack('<H', len(files)) + entries)
        print('Embedded desktop app seed: ' + str(len(files)) + ' files; ' + str(args.archive.stat().st_size) + ' bytes')
        return
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in files:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    if (output / 'config.json').exists():
        raise SystemExit('Recovery seeds must not overwrite existing Unveil preferences')
    print('Staged desktop Unveil resources: ' + str(len(files)))


if __name__ == '__main__':
    main()
