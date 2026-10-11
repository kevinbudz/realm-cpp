#!/usr/bin/env python3
"""Recreate every animated-ground composition and compare original TileRedrawer."""
import argparse
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib
from PIL import Image, ImageChops

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--flash-root', type=Path, default=Path(__file__).resolve().parents[2] / 'realm-client')
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
p.add_argument('--java-home', type=Path, default=Path.home() / '.local/share/air-jdk')
a = p.parse_args(); a.build = a.build.resolve()
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_flash_reference import build
grounds = ET.parse(a.flash_root / 'src/kabam/rotmg/assets/EmbeddedData_GroundCXML.dat').getroot()
animated = [xml.attrib['id'] for xml in grounds if xml.find('Animate') is not None or xml.find('TopAnimate') is not None]
assert animated, 'No animated source grounds found'
width = 10; height = ((len(animated)+1)//2) * 10
tiles = [0] * (width * height)
for index in range(len(animated)):
    ox = 1 + index % 2 * 5; oy = 1 + index // 2 * 10
    for y in range(oy, oy+3):
        for x in range(ox, ox+3):
            tiles[y*width+x] = index+1
directory = a.build / 'animated-terrain-reference'; directory.mkdir(parents=True, exist_ok=True)
map_path = directory / 'animated.jm'
map_path.write_text(json.dumps(dict(width=width, height=height, dict=[dict(ground='Grass')] + [dict(ground=name) for name in animated],
                                   data=base64.b64encode(zlib.compress(struct.pack('>' + str(len(tiles)) + 'h', *tiles))).decode())))
subprocess.run([str(a.build / 'realm_terrain_inputs'), str(map_path), str(directory), '0'], check=True)
reference = a.build / 'flash-ground-fixed'
descriptor = build('GroundExporter', a.flash_root.resolve(), a.sdk.resolve(), a.java_home.resolve(), reference, mask_variant=0)
subprocess.run([str(a.sdk / 'bin/adl'), str(descriptor), '--', str(map_path), str(directory / 'tasks.json')], check=True, timeout=120)
tasks = json.loads((directory / 'tasks.json').read_text()); failures = []
for task in tasks:
    expected = Image.open(task['output']).convert('RGBA')
    native = Image.open(directory / 'native' / Path(task['output']).name).convert('RGBA')
    if expected.size != native.size or expected.tobytes() != native.tobytes():
        failures.append(Path(task['output']).name)
        if expected.size == native.size:
            ImageChops.difference(expected, native).save(directory / ('difference-' + failures[-1]))
counts = {mode: sum(task['mode'] == mode for task in tasks) for mode in ('base', 'overlay', 'composed')}
(directory / 'comparison.json').write_text(json.dumps(dict(animated_grounds=animated, cases=len(tasks), modes=counts, source_failures=failures), indent=2) + '\n')
assert not failures, f'Animated terrain mismatches: {failures}'
print(f'{len(tasks)} compositions for {len(animated)} animated grounds match original Flash exactly: {counts}')
