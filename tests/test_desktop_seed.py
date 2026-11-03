#!/usr/bin/env python3
"""Exercise the actual desktop seed installer against a temporary filesystem."""
import ctypes
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

repo = Path(__file__).resolve().parents[1]
project = repo / 'firmware/mosaico'
work = Path(tempfile.mkdtemp(prefix='unveil-seed-test-'))
archive = work / 'seed.bin'
subprocess.run([sys.executable, str(project / 'tools/stage_unveil_app.py'),
                '--app', str(project / 'app'), '--archive', str(archive)], check=True)
raw = archive.read_bytes()
assert raw[:8] == b'UVSEED01' and hashlib.sha256(raw[42:]).digest() == raw[8:40]
entries = {}
offset = 42
for _ in range(struct.unpack_from('<H', raw, 40)[0]):
    length, size, crc = struct.unpack_from('<HII', raw, offset)
    offset += 10
    name = raw[offset:offset + length].decode()
    offset += length
    data = raw[offset:offset + size]
    offset += size
    assert zlib.crc32(data) == crc
    entries[name] = data
assert offset == len(raw) and 'config.json' not in entries
assert list(entries)[-1] == 'launcher.json'

library = work / ('seed.dylib' if sys.platform == 'darwin' else 'seed.so')
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DUNVEIL_SEED_HOST_TEST', '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                '-fPIC', str(project / 'main/unveil_seed.c'), '-o', str(library)], check=True)
install = ctypes.CDLL(str(library)).unveil_seed_install
install.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p]
install.restype = ctypes.c_int


def apply(data, directory):
    buffer = ctypes.create_string_buffer(data)
    return install(buffer, len(data), str(directory).encode())


fresh = work / 'fresh/app'
assert apply(raw, fresh) == 0
assert all((fresh / name).read_bytes() == data for name, data in entries.items())
assert apply(raw, fresh) == 0
preferences = b'{"keep_user_settings":true}'
(fresh / 'config.json').write_bytes(preferences)
(fresh / 'launcher.json').unlink()
assert apply(raw, fresh) == 0
assert (fresh / 'launcher.json').read_bytes() == entries['launcher.json']
assert (fresh / 'config.json').read_bytes() == preferences
(fresh / '.unveil-fw-seed').unlink()
(fresh / 'scripts/launch.lua').write_bytes(b'interrupted previous update')
assert apply(raw, fresh) == 0
assert (fresh / 'scripts/launch.lua').read_bytes() == entries['scripts/launch.lua']
assert (fresh / 'config.json').read_bytes() == preferences
broken = bytearray(raw)
broken[-1] ^= 1
assert apply(bytes(broken), fresh) != 0
assert (fresh / 'config.json').read_bytes() == preferences
name, data = b'../escape', b'x'
entry = struct.pack('<HII', len(name), len(data), zlib.crc32(data)) + name + data
unsafe = b'UVSEED01' + hashlib.sha256(entry).digest() + struct.pack('<H', 1) + entry
assert apply(unsafe, fresh) != 0 and not (fresh.parent / 'escape').exists()
print('DESKTOP_SEED_INSTALL_OK fresh, repeat, preferences, missing-launcher, interrupted-update, CRC, path-boundary')
