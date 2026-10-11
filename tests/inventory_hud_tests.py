#!/usr/bin/env python3
"""Compare native assembly and GPU uploads to the original inventory classes."""
import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--flash-root', type=Path, default=Path(__file__).resolve().parents[2] / 'realm-client')
p.add_argument('--fonts', type=Path)
p.add_argument('--art', type=Path)
p.add_argument('--native-only', action='store_true')
p.add_argument('--rebuild-reference', action='store_true')
a = p.parse_args(); a.build = a.build.resolve()
fonts = (a.fonts or a.build / 'fonts-client').resolve(); art = (a.art or a.build / 'hud-art').resolve()
directory = a.build / 'inventory-reference'; directory.mkdir(parents=True, exist_ok=True)
definitions = {}
for file in sorted((a.flash_root / 'src/kabam/rotmg/assets').glob('*CXML.dat')):
    if 'Ground' in file.name or 'Regions' in file.name or 'Test' in file.name or 'Temp' in file.name:
        continue
    for node in ET.parse(file).getroot():
        if 'type' in node.attrib:
            definitions[int(node.attrib['type'], 0)] = node
players = [(key, xml) for key, xml in definitions.items() if xml.findtext('Class') == 'Player']
art_keys = {entry['key'] for entry in json.loads((art / 'manifest.json').read_text())['images']}
items = sorted(key for key, xml in definitions.items() if xml.findtext('Class') == 'Equipment' and f'item{key}' in art_keys)
cases = []
def case(type, items, mp=1000, backpack=False, selected=0, tiers=True):
    cases.append(dict(id=str(len(cases)), type=type, items=(items + [-1] * 20)[:20], mp=mp,
                      backpack=backpack, selected=selected, tiers=tiers))
for type, xml in players:
    start = [int(value.strip(), 0) for value in xml.findtext('Equipment').split(',')]
    case(type, [-1] * 20)
    case(type, start, mp=0)
    case(type, start)
    case(type, start, tiers=False)
    case(type, start + items[:8], backpack=True, selected=2)
for index in range(0, len(items), 8):
    case(0x030e, [-1] * 4 + items[index:index+8])
case(0x030e, [-1] * 20, backpack=True)
case(0x030e, [-1] * 20, backpack=True, selected=2)
# Restore a populated fixture after empty/backpack states to verify dirty uploads.
case(0x030e, items[:20])
tasks = directory / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(directory / 'flash'), art=str(art), cases=cases, stress=True)))
subprocess.run([str(a.build / 'realm_hud_inputs'), str(fonts), str(tasks)], check=True)
gpu_tool = a.build / 'realm_hud_gpu_inputs'
if gpu_tool.exists():
    subprocess.run([str(gpu_tool), str(fonts), str(tasks)], check=True)
if not a.native_only:
    descriptor = a.build / 'flash-inventory-reference/InventoryExporter-app.xml'
    if a.rebuild_reference or not descriptor.exists():
        subprocess.run(['python3', str(Path(__file__).resolve().parents[1] / 'tools/build_flash_reference.py'), 'InventoryExporter', '--flash-root', str(a.flash_root.resolve()), '--output', str(descriptor.parent)], check=True)
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=120)
failures = []
for data in cases:
    filename = data['id'] + '.png'; native = Image.open(directory / 'native' / filename).convert('RGBA')
    if gpu_tool.exists():
        gpu = Image.open(directory / 'native-gpu' / filename).convert('RGBA')
        assert native.tobytes() == gpu.tobytes(), f'Incremental GPU fixture {filename} differs'
    if not a.native_only:
        expected = Image.open(directory / 'flash' / filename).convert('RGBA')
        portrait_diff = ImageChops.difference(expected.crop((0,190,200,230)), native.crop((0,190,200,230)))
        if portrait_diff.getbbox(alpha_only=False):
            portrait_diff.save(directory / ('portrait-difference-' + filename))
            failures.append((data['id'], 'portrait', portrait_diff.getbbox(alpha_only=False)))
        diff = ImageChops.difference(expected.crop((0,300,200,500)), native.crop((0,300,200,500)))
        if diff.getbbox(alpha_only=False):
            diff.save(directory / ('difference-' + filename))
            failures.append((data['id'], sum(any(pixel) for pixel in diff.get_flattened_data()), diff.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), items=len(items), source_failures=failures, native_only=a.native_only), indent=2) + '\n')
assert not failures, f'Original inventory differences: {failures}'
print(f'{len(cases)} retained panels, including {len(items)} distinct items, match reconstruction and GPU uploads')
if not a.native_only:
    print('Class portraits and equipment/inventory regions match original Flash exactly')
