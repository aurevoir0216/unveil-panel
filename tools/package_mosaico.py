#!/usr/bin/env python3
"""Package three built Mosaico variants, without opening or writing any device."""
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
    parser.add_argument('--standalone-build', type=Path, required=True)
    parser.add_argument('--desktop-build', type=Path, required=True)
    parser.add_argument('--desktop-bundle', type=Path, required=True)
    parser.add_argument('--idea-bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default='0.20')
    args = parser.parse_args()
    import sys
    repo = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='unveil-package-'))
    independent = temporary / ('unveil-mosaico-standalone-v' + args.version)
    images = independent / 'images'
    images.mkdir(parents=True)
    flash = json.loads((args.standalone_build / 'flasher_args.json').read_text())
    mapped = []
    merge = [sys.executable, '-m', 'esptool', '--chip', 'esp32s31', 'merge-bin',
             '--target-offset', '0', '--pad-to-size', '16MB', '--output',
             str(independent / 'factory.bin')]
    for key, option in [('flash_mode', '--flash-mode'), ('flash_freq', '--flash-freq'), ('flash_size', '--flash-size')]:
        merge += [option, flash['flash_settings'][key]]
    for offset, name in sorted(flash['flash_files'].items(), key=lambda item: int(item[0], 0)):
        source = args.standalone_build / name
        target = images / source.name
        if target.exists():
            raise ValueError('Duplicate image basename: ' + target.name)
        shutil.copy2(source, target)
        mapped.append({'offset': offset, 'file': 'images/' + target.name, 'bytes': target.stat().st_size})
        merge += [offset, str(target)]
    subprocess.run(merge, check=True, stdout=subprocess.DEVNULL)
    factory = (independent / 'factory.bin').read_bytes()
    assert len(factory) == 16 * 1024 * 1024
    for entry in mapped:
        data = (independent / entry['file']).read_bytes()
        start = int(entry['offset'], 0)
        assert factory[start:start + len(data)] == data, entry['file']
    (independent / 'flash-layout.json').write_text(json.dumps({
        'variant': 'standalone', 'chip': 'esp32s31', 'factory_offset': '0x0',
        'replaces_mosaico_flash_layout': True, 'hardware_accepted': False,
        'images': mapped}, indent=2) + '\n')
    (independent / 'INSTALL.txt').write_text(
        'Standalone build candidate, not yet hardware-accepted.\n'
        'This is a deliberate full Flash conversion and replaces Mosaico Vibe Mode.\n'
        'Only factory.bin is the merged image for address 0. Separate images use flash-layout.json offsets.\n'
        'No device write is performed by packaging. Back up your device before choosing installation.\n'
        'Wi-Fi and service settings start blank; configure them in Unveil System.\n'
        'Do not use the old Mosaico 0x210000 main-app address for this independent layout.\n')
    shutil.copy2(repo / 'firmware/mosaico-standalone/README.md', independent / 'README.md')
    shutil.copy2(repo / 'LICENSE', independent / 'LICENSE')
    archive(independent, output / (independent.name + '.zip'))

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
    (desktop / 'INSTALL.txt').write_text(
        'Desktop build candidate, not yet hardware-accepted.\n'
        'Install unveil-desktop.irisfw through the compatible retained Vibe Mode update workflow.\n'
        'It updates the normal app, UI and system resources, not the retained Vibe Mode prefix.\n'
        'After Mosaico starts and connects, run: python tools/install_app.py http://DEVICE_IP\n'
        'Existing NAND Unveil preferences are preserved. Restart after all resources are installed.\n'
        'Open the round Unveil desktop icon. Primary-page upward swipe exits; secondary swipe goes back.\n'
        'No personal credentials, service address, NVS or acquisition data are supplied.\n')
    archive(desktop, output / (desktop.name + '.zip'))
    idea = output / ('unveil-mosaico-idea-v' + args.version + '.irisfw')
    shutil.copy2(args.idea_bundle, idea)
    records = []
    for file in [output / (independent.name + '.zip'), output / (desktop.name + '.zip'), idea]:
        records.append({'file': file.name, 'bytes': file.stat().st_size,
                        'sha256': hashlib.sha256(file.read_bytes()).hexdigest()})
    (output / 'artifact-audit.json').write_text(json.dumps(records, indent=2) + '\n')
    print(json.dumps({'prepared': [item['file'] for item in records], 'installed': False}, indent=2))


if __name__ == '__main__':
    main()
