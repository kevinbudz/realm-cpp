#!/usr/bin/env python3
"""Measure how Flash renders premultiplied pixels under PartyPanel's ColorTransforms.

PartyPanel uses darkCT (0.6), the low-HP pulse (offsets +-step*12.8) and their
concatenation. The probe covers every reachable alpha/premultiplied pair. The
native model is clamp(floor(p*m8/256 + offset*a/255), 0, a), with the 8.8
multiplier and the offset truncated toward zero; the output stores the sorted
(multiplier, offset, alpha, premultiplied) keys where Flash differs by +-1.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image
from build_flash_reference import build
p = argparse.ArgumentParser(description=__doc__)
root = Path(__file__).resolve().parents[1]
p.add_argument('--output', type=Path, default=root / 'assets')
p.add_argument('--work', type=Path, default=root / 'build/party-transforms')
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
p.add_argument('--java-home', type=Path, default=Path.home() / '.local/share/air-jdk')
a = p.parse_args()
a.work = a.work.resolve(); a.output = a.output.resolve()
a.work.mkdir(parents=True, exist_ok=True); a.output.mkdir(parents=True, exist_ok=True)
descriptor = build('InventoryExporter', root.parent / 'realm-client', a.sdk, a.java_home, a.work / 'reference')
probe = a.work / 'probe'
tasks = a.work / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(probe), probe=dict(backdrops=[0, 0x363636]))))
subprocess.run([str(a.sdk / 'bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=180)

def load(dark, step, backdrop):
    return Image.open(probe / f'alpha-d{dark}-s{step}-b{backdrop:x}.png').convert('RGB').load()

def model(m8, offset, alpha, value):
    return max(0, min(alpha, (value * m8 * 255 + offset * alpha * 256) // 65280))

base = load(0, -1, 0)
tables = {}
for dark in (0, 1):
    for step in range(-1, 10):
        if not dark and step <= 0:
            continue  # Identity transforms are not applied.
        black, panel = load(dark, step, 0), load(dark, step, 0x363636)
        k = math.trunc(step * 12.8) if step > 0 else 0
        for x in range(256):
            for alpha in range(1, 256):
                for c, offset in enumerate((k, -k, -k)):
                    stored, result = base[x, alpha][c], black[x, alpha][c]
                    # Transformed pixels then composite like any premultiplied pixel.
                    if panel[x, alpha][c] != result + (54 * (256 - alpha) >> 8):
                        raise AssertionError('Transformed pixel does not composite as premultiplied source')
                    table = tables.setdefault((153 if dark else 256, offset), {})
                    if table.setdefault((alpha, stored), result) != result:
                        raise AssertionError('Transform is not determined by premultiplied channel and alpha')
corrections = []
for (m8, offset), table in tables.items():
    if len(table) != 32895:
        raise AssertionError(f'Incomplete probe for multiplier {m8}, offset {offset}')
    for (alpha, stored), result in table.items():
        delta = result - model(m8, offset, alpha, stored)
        if abs(delta) > 1:
            raise AssertionError('Correction exceeds one level')
        if delta:
            key = (int(m8 == 153) << 24) | ((offset + 128) << 16) | (alpha << 8) | stored
            corrections.append(key << 1 | (delta > 0))
corrections.sort()
binary = a.output / 'party-transforms.bin'
data = struct.pack(f'<{len(corrections)}I', *corrections)
binary.write_bytes(data)
(a.output / 'party-transforms.json').write_text(json.dumps(dict(
    schema=1, sdk=ET.parse(a.sdk / 'airsdk.xml').getroot().tag.split('}')[0].rsplit('/', 1)[1],
    transforms=len(tables), corrections=len(corrections), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()), indent=2) + '\n')
print(f'{len(tables)} channel transforms measured; {len(corrections)} corrections to the native model')
