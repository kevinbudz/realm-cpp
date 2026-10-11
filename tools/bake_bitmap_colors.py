#!/usr/bin/env python3
"""Measure BitmapData.getPixel32's internal color recovery lookup table."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image
from build_flash_reference import build
p = argparse.ArgumentParser(description=__doc__)
root = Path(__file__).resolve().parents[1]
p.add_argument('--output', type=Path, default=root / 'assets')
p.add_argument('--work', type=Path, default=root / 'build/bitmap-colors')
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
p.add_argument('--java-home', type=Path, default=Path.home() / '.local/share/air-jdk')
a = p.parse_args()
a.work = a.work.resolve(); a.output = a.output.resolve()
a.work.mkdir(parents=True, exist_ok=True); a.output.mkdir(parents=True, exist_ok=True)
palette = Image.new('RGBA', (256, 256))
palette.putdata([(color, color, color, alpha) for alpha in range(256) for color in range(256)])
palette.save(a.work / 'palette.png')
descriptor = build('BitmapColorExporter', root.parent / 'realm-client', a.sdk, a.java_home, a.work / 'reference')
output = a.work / 'palette.bin'
subprocess.run([str(a.sdk / 'bin/adl'), str(descriptor), '--', str(a.work / 'palette.png'), str(output)], check=True)
values = struct.unpack('>65536I', output.read_bytes())
internal = Image.open(str(output) + '-black.png').convert('RGB')
table = bytearray(65536)
seen = set()
for alpha in range(256):
    for color in range(256):
        expected = values[alpha*256+color]
        if expected >> 24 != alpha or (expected & 255) != (expected >> 8 & 255) or (expected & 255) != (expected >> 16 & 255):
            raise AssertionError('Unexpected alpha or channel-dependent color recovery')
        stored = internal.getpixel((color, alpha))[0]
        key = alpha*256+stored
        recovered = expected & 255
        if key in seen and table[key] != recovered:
            raise AssertionError('Color recovery is not determined by premultiplied color and alpha')
        table[key] = recovered; seen.add(key)
    # Values above alpha cannot occur in a premultiplied image.
    for stored in range(256):
        key = alpha*256+stored
        if key not in seen:
            table[key] = min(255, stored*256//alpha) if alpha else 0
binary = a.output / 'bitmap-colors.bin'
binary.write_bytes(table)
(a.output / 'bitmap-colors.json').write_text(json.dumps(dict(schema=1, sdk=ET.parse(a.sdk / 'airsdk.xml').getroot().tag.split('}')[0].rsplit('/',1)[1], bytes=len(table),
    sha256=hashlib.sha256(table).hexdigest(), measured=len(seen), samples=65536), indent=2) + '\n')
print(f'{len(seen)} reachable color/alpha pairs measured from 65536 source samples')
