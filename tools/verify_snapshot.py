#!/usr/bin/env python3
"""Verify the packaged source, resources and packaged firmware checksums."""
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / 'SNAPSHOT_SHA256.json').read_text())
    failures = []
    for name, expected in manifest.items():
        path = root / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            failures.append(name)
    platform = root / 'firmware/mosaico'
    spec = json.loads((platform / 'snapshot.json').read_text())
    binary = platform / spec['firmware']['path']
    if binary.stat().st_size != spec['firmware']['bytes']:
        failures.append('firmware size differs')
    if hashlib.sha256(binary.read_bytes()).hexdigest() != spec['firmware']['sha256']:
        failures.append('firmware SHA-256 differs')
    assert spec['firmware']['flash_address'] == '0x210000'
    for config in [platform / 'app/config.example.json', root / 'firmware/mosaico-core/resources/apps/unveil/config.json']:
        if json.loads(config.read_text()).get('pi_url') != '':
            failures.append('Service default must remain blank: ' + str(config.relative_to(root)))
    for directory in [platform / 'app/scripts', root / 'firmware/mosaico-core/resources/apps/unveil/scripts']:
        if "default=''" not in (directory / 'endpoint.lua').read_text():
            failures.append('Embedded service default must remain blank')
    sdk = (platform / 'sdkconfig.release').read_text()
    if 'CONFIG_APP_WIFI_SSID=""' not in sdk or 'CONFIG_APP_WIFI_PASSWORD=""' not in sdk:
        failures.append('Personal Wi-Fi must not be packaged')
    if failures:
        raise SystemExit('Snapshot verification failed:\n' + '\n'.join(failures))
    print(f'Verified {len(manifest)} packaged files and legacy Mosaico firmware.')


if __name__ == '__main__':
    main()
