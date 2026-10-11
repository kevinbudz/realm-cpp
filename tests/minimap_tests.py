#!/usr/bin/env python3
"""Compare map colors and actual MiniMap.draw output with the Flash source."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--rebuild-reference', action='store_true')
p.add_argument('--angles', type=float, nargs='+', default=[0, math.pi/4, 7*math.pi/4])
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
a = p.parse_args()
a.build = a.build.resolve()
root = Path(__file__).resolve().parents[2]
directory = a.build / 'minimap-reference'
directory.mkdir(parents=True, exist_ok=True)
inventory = json.loads((a.build / 'generated/source_inventory.json').read_text())
grounds, objects = {}, {}
for entry in inventory['registered_data']:
    doc = ET.parse(root / 'realm-client' / entry['path']).getroot()
    for node in doc.findall('Ground'):
        grounds[node.attrib['id']] = int(node.attrib['type'], 0)
    for node in doc.findall('Object'):
        objects[node.attrib['id']] = (int(node.attrib['type'], 0), node.findtext('Class'), node.find('Enemy') is not None)
ground_types = list(grounds.values())
wizard = objects['Wizard'][0]
enemy = next(value[0] for value in objects.values() if value[1] == 'Character' and value[2])
portal = next(value[0] for value in objects.values() if value[1] == 'Portal')
cases = []; maps = []
for width, height in [(142, 130), (400, 200), (30, 40)]:
    tiles = [dict(x=x, y=y, type=ground_types[(x//4 + y//4*7) % len(ground_types)])
             for y in range(height) for x in range(width) if (x + y) % 11 != 0]
    map_index = len(maps); maps.append(dict(width=width, height=height, tiles=tiles))
    for zoom in range(4):
        for location, focus in [('center', [width/2+.125, height/2+.375]), ('edge', [1.5, height-2.5])]:
            for angle in a.angles:
                markers = [dict(type=wizard, id=1, x=focus[0]+3.25, y=focus[1]-4.125, fellow=False),
                           dict(type=wizard, id=2, x=width-1.5, y=height-1.5, fellow=True),
                           dict(type=enemy, id=3, x=focus[0]-6, y=focus[1]+5.25, fellow=False),
                           dict(type=portal, id=4, x=width-.5, y=.5, fellow=False)]
                cases.append(dict(id=str(len(cases)), map=map_index, focus=focus,
                                  zoom=zoom, angle=angle, markers=markers))
tasks = directory / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(directory / 'flash'), maps=maps, cases=cases)))
descriptor = a.build / 'flash-minimap-reference/MinimapExporter-app.xml'
if a.rebuild_reference or not descriptor.exists():
    subprocess.run(['python3', str(root / 'realm-cpp/tools/build_flash_reference.py'), 'MinimapExporter',
                    '--output', str(descriptor.parent), '--sdk', str(a.sdk)], check=True)
subprocess.run([str(a.sdk / 'bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=180)
subprocess.run([str(a.build / 'realm_minimap_inputs'), str(tasks)], check=True, timeout=180)
source = json.loads((directory / 'flash/colors.json').read_text())
native = json.loads((directory / 'native/colors.json').read_text())
color_failures = [(kind, key, value, native[kind].get(key)) for kind in source
                  for key, value in source[kind].items() if native[kind].get(key) != value & 0xffffff]
failures = []
for case in cases:
    filename = case['id'] + '.png'
    expected = Image.open(directory / 'flash' / filename).convert('RGBA')
    actual = Image.open(directory / 'native' / filename).convert('RGBA')
    if expected.tobytes() != actual.tobytes():
        diff = ImageChops.difference(expected, actual)
        diff.save(directory / 'native' / (case['id'] + '-difference.png'))
        pixels = sum(any(pixel) for pixel in diff.get_flattened_data())
        failures.append((case['id'], pixels, diff.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(colors=sum(len(colors) for colors in source.values()), cases=len(cases), exact=len(cases)-len(failures), color_failures=color_failures, failures=failures), indent=2) + '\n')
if color_failures or failures:
    raise AssertionError(f'Colors differ: {color_failures}; {len(failures)}/{len(cases)} minimap frames differ: {failures}')
print(f'{sum(len(colors) for colors in source.values())} source colors and {len(cases)} minimap frames match exactly')
