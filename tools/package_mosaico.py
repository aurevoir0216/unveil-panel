#!/usr/bin/env python3
"""Package the two maintained Mosaico variants, without opening or writing any device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def archive(folder, output):
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as package:
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                package.write(path, folder.name + '/' + path.relative_to(folder).as_posix())
    with zipfile.ZipFile(output) as package:
        if package.testzip() is not None:
            raise RuntimeError('ZIP validation failed: ' + str(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--desktop-build', type=Path, required=True)
    parser.add_argument('--desktop-bundle', type=Path, required=True)
    parser.add_argument('--idea-bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default='0.1')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='unveil-package-'))
    desktop = temporary / ('unveil-mosaico-desktop-v' + args.version)
    desktop.mkdir()
    shutil.copy2(args.desktop_bundle, desktop / 'unveil-desktop.irisfw')
    shutil.copytree(repo / 'firmware/mosaico/app', desktop / 'app')
    required_assets = {'Doto-Regular.ttf', 'Doto-Metrics.ttf', 'Unveil-Text-Readable.ttf',
                       'Doto-OFL.txt', 'Noto-OFL.txt', 'icon.jpg'}
    for asset in (desktop / 'app/assets').iterdir():
        if asset.is_file() and asset.name not in required_assets:
            asset.unlink()

    (desktop / 'tools').mkdir()
    shutil.copy2(repo / 'firmware/mosaico/tools/install_app.py', desktop / 'tools/install_app.py')
    shutil.copy2(repo / 'firmware/mosaico/README.md', desktop / 'README.md')
    shutil.copy2(repo / 'LICENSE', desktop / 'LICENSE')
    shutil.copy2(repo / 'THIRD_PARTY.md', desktop / 'THIRD_PARTY.md')
    (desktop / 'INSTALL.txt').write_text(
        'Mosaico desktop application firmware. See validation notes for acceptance scope.\n'
        'Install unveil-desktop.irisfw through the compatible retained Vibe Mode update workflow.\n'
        'It updates the normal app, UI and system resources, not the retained Vibe Mode prefix.\n'
        'The firmware seeds the Unveil desktop app automatically on startup.\n'
        'tools/install_app.py is optional for script-only development updates.\n'
        'Existing NAND Unveil preferences are preserved during automatic app installation.\n'
        'Open the round Unveil desktop icon. Primary-page upward swipe exits; secondary swipe goes back.\n'
        'No personal credentials, service address, NVS or acquisition data are supplied.\n')
    archive(desktop, output / (desktop.name + '.zip'))
    idea = output / ('unveil-mosaico-idea-v' + args.version + '.irisfw')
    shutil.copy2(args.idea_bundle, idea)
    records = []
    for file in [output / (desktop.name + '.zip'), idea]:
        records.append({'file': file.name, 'bytes': file.stat().st_size,
                        'sha256': hashlib.sha256(file.read_bytes()).hexdigest()})
    (output / 'artifact-audit.json').write_text(json.dumps(records, indent=2) + '\n')
    print(json.dumps({'prepared': [item['file'] for item in records], 'installed': False}, indent=2))


if __name__ == '__main__':
    main()
