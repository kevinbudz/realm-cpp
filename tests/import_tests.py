#!/usr/bin/env python3
"""Check that the generated native metadata agrees with the Flash source assets."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

spec = importlib.util.spec_from_file_location('import_flash', Path(__file__).parents[1] / 'tools/import_flash.py')
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)
root = Path(sys.argv[1]).resolve()
data = importer.scan(root)
assert data['packets']['MOVE'] == 4 and data['packets']['UPDATE'] == 9
assert data['packets']['NEWTICK'] == 11 and data['packets']['HELLO'] == 15
assert data['string_stats'] == [data['stats']['NAME_STAT'], data['stats']['GUILD_NAME_STAT']]
assert data['stats']['ACCOUNT_ID_STAT'] not in data['string_stats']
assert len({s['name'] for s in data['sheets']}) == len(data['sheets'])
for asset in data['assets']:
    raw = (root / asset['path']).read_bytes()
    assert struct.unpack('>II', raw[16:24]) == (asset['width'], asset['height'])
    assert hashlib.sha256(raw).hexdigest() == asset['sha256']
for sheet in data['sheets']:
    if sheet['asset'] < 0:
        assert sheet['name'] == 'invisible' and sheet['count'] == 1
        continue
    asset = data['assets'][sheet['asset']]
    assert sheet['count'] == (asset['width'] // sheet['width']) * (asset['height'] // sheet['height'])
for sheet in data['animated']:
    asset = data['assets'][sheet['asset']]
    assert sheet['strip_width'] == sheet['width'] * 7
    assert sheet['strip_height'] % sheet['height'] == 0
    assert asset['width'] >= sheet['strip_width'] and asset['height'] >= sheet['strip_height']
    if sheet['mask'] >= 0:
        mask = data['assets'][sheet['mask']]
        assert (mask['width'], mask['height']) == (asset['width'], asset['height'])
assert data['definitions'] and sum(d['ground'] for d in data['definitions']) > 0
print(f"Validated {len(data['assets'])} original PNGs, {len(data['sheets'])} image sets, "
      f"{len(data['animated'])} animation registrations, and this fork's packet/stat IDs")
