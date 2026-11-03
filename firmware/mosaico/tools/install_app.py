#!/usr/bin/env python3
"""Upload current Mosaico app resources, preserving existing user settings."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('device', help='Device base URL, e.g. http://192.168.1.20')
    args = parser.parse_args()
    url = urllib.parse.urlsplit(args.device)
    if url.scheme not in ('http', 'https') or not url.netloc or url.path not in ('', '/') or url.query or url.fragment or url.username or url.password:
        parser.error('Use a device base URL without credentials, path, query or fragment')
    base = args.device.rstrip('/')
    app = Path(__file__).resolve().parents[1] / 'app'

    def request(path, data=None, json_body=False):
        headers = {}
        if json_body:
            data = json.dumps(data).encode()
            headers['Content-Type'] = 'application/json'
        elif data is not None:
            headers['Content-Type'] = 'application/octet-stream'
        req = urllib.request.Request(base + path, data=data, headers=headers)
        with urllib.request.urlopen(req, timeout=30) as response:
            return response.read()

    try:
        request('/files/apps/unveil/config.json')
        existing_config = True
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        existing_config = False
    for suffix in ('', '/scripts', '/assets'):
        request('/api/files/mkdir', {'path': '/apps/unveil' + suffix, 'recursive': True}, True)
    files = {}
    for folder in ('scripts', 'assets'):
        for path in (app / folder).rglob('*'):
            if path.is_file():
                files[path.relative_to(app).as_posix()] = path.read_bytes()
    if not existing_config:
        files['config.json'] = (app / 'config.example.json').read_bytes()
    files['launcher.json'] = (app / 'launcher.json').read_bytes()
    for name in sorted(k for k in files if k != 'launcher.json') + ['launcher.json']:
        target = '/apps/unveil/' + name
        request('/api/files/upload?' + urllib.parse.urlencode({'path': target}), files[name])
        actual = request('/files' + urllib.parse.quote(target, safe='/'))
        if hashlib.sha256(actual).digest() != hashlib.sha256(files[name]).digest():
            raise SystemExit('Read-back mismatch: ' + name)
        print('Verified:', name)
    print('Resources verified. Existing settings preserved.' if existing_config else 'Resources verified. Default settings installed.')
    print('Restart Mosaico manually to load the complete version.')


if __name__ == '__main__':
    main()
