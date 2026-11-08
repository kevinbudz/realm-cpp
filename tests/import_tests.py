#!/usr/bin/env python3
"""Check that the vendored game assets agree with assets/manifest.json and that the tables built from it hold.

Run with the assets directory (default: assets/): python3 tests/import_tests.py assets
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).parents[1] / 'assets').resolve()
manifest = json.loads((root / 'manifest.json').read_text())
assert manifest['schema'] == 1
packets, stats = manifest['packets'], manifest['stats']
assert packets['MOVE'] == 4 and packets['UPDATE'] == 9
assert packets['NEWTICK'] == 11 and packets['HELLO'] == 15
assert manifest['string_stats'] == [stats['NAME_STAT'], stats['GUILD_NAME_STAT']]
assert stats['ACCOUNT_ID_STAT'] not in manifest['string_stats']
assert len({s['name'] for s in manifest['sheets']}) == len(manifest['sheets'])

by_file = {f['file']: f for f in manifest['files']}
for f in manifest['files']:
    assert hashlib.sha256((root / f['file']).read_bytes()).hexdigest() == f['sha256'], f['file']
    assert not f['original'].endswith('.as'), f['original']
originals = [by_file[image['file']]['original'] for image in manifest['images']]
assert originals == sorted(originals), 'image table must stay in sorted original-path order'
for image in manifest['images']:
    raw = (root / image['file']).read_bytes()
    assert struct.unpack('>II', raw[16:24]) == (image['width'], image['height']), image['file']
for sheet in manifest['sheets']:
    if sheet['asset'] < 0:
        assert sheet['name'] == 'invisible' and sheet['count'] == 1
        continue
    asset = manifest['images'][sheet['asset']]
    assert sheet['count'] == (asset['width'] // sheet['width']) * (asset['height'] // sheet['height'])
for sheet in manifest['animated']:
    asset = manifest['images'][sheet['asset']]
    assert sheet['strip_width'] == sheet['width'] * 7
    assert sheet['strip_height'] % sheet['height'] == 0
    assert asset['width'] >= sheet['strip_width'] and asset['height'] >= sheet['strip_height']
    if sheet['mask'] >= 0:
        mask = manifest['images'][sheet['mask']]
        assert (mask['width'], mask['height']) == (asset['width'], asset['height'])
grounds = 0
for data_file in manifest['data_files']:
    assert data_file['kind'] in ('groundFiles', 'objectFiles', 'regionFiles'), data_file
    xml = ET.parse(root / data_file['file']).getroot()
    grounds += len(xml.findall('Ground'))
assert grounds > 0
assert manifest['models'] and all((root / m['file']).is_file() for m in manifest['models'])
print(f"Validated {len(manifest['images'])} images, {len(manifest['sheets'])} image sets, "
      f"{len(manifest['animated'])} animation registrations, {len(manifest['models'])} models, "
      f"{len(manifest['data_files'])} data files and this fork's packet/stat IDs")
