#!/usr/bin/env python3
"""Compare potion stack widgets with original PotionInventoryView/PotionSlotView."""
import argparse
import io
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=ROOT / 'build')
p.add_argument('--flash-root', type=Path, default=ROOT.parent / 'realm-client')
p.add_argument('--fonts', type=Path)
p.add_argument('--art', type=Path)
p.add_argument('--native-only', action='store_true')
p.add_argument('--rebuild-reference', action='store_true')
a = p.parse_args()
a.build = a.build.resolve()
fonts = (a.fonts or a.build / 'fonts-client').resolve()
art = (a.art or a.build / 'hud-art').resolve()
directory = a.build / 'potion-reference'
directory.mkdir(parents=True, exist_ok=True)
players = [node for node in ET.parse(a.flash_root / 'src/kabam/rotmg/assets/EmbeddedData_PlayersCXML.dat').getroot()
           if node.findtext('Class') == 'Player']
cases = []

def case(player, potions, selected=0, backpack=False, lower=False, **extra):
    start = [int(value.strip(), 0) for value in player.findtext('Equipment').split(',')]
    items = (start + [-1] * 20)[:20]
    if lower:
        # Bottom grid rows lie under the potion icons, which extend 11 pixels above the buttons.
        for slot in (8, 9, 10, 11, 16, 17, 18, 19):
            items[slot] = start[slot % 4]
    cases.append(dict(id=str(len(cases)), type=int(player.attrib['type'], 0), mp=100, backpack=backpack,
                      selected=selected, tiers=True, items=items, potions=potions, **extra))

for player in players:
    case(player, [0, 0])
    case(player, [1, 6], lower=True)
    cases[-1]['regions'] = [[0, 300, 200, 200]]
    case(player, [6, 1], lower=True, backpack=True)
    case(player, [2, 0], selected=2, backpack=True, lower=True)
    case(player, [3, 4], selected=2, backpack=True)
    case(player, [5, 5], selected=0, backpack=True)
    case(player, [0, 3], selected=0, backpack=True)
    case(player, [10, 123], selected=0, backpack=True)
    case(player, [10, 123], selected=0, backpack=True, redraw=False)

bundle = (art / 'images.bin').read_bytes()
for record in json.loads((art / 'manifest.json').read_text())['images']:
    if record['key'] in ('potion2594', 'potion2595'):
        size = Image.open(io.BytesIO(bundle[record['offset']:record['offset'] + record['length']])).size
        assert size == (46, 35), f'HudInput::potion_at assumes 46x35 icons, found {record["key"]} {size}'

tasks = directory / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(directory / 'flash'), art=str(art), cases=cases)))
subprocess.run([str(a.build / 'realm_hud_inputs'), str(fonts), str(tasks)], check=True)
gpu_tool = a.build / 'realm_hud_gpu_inputs'
if gpu_tool.exists():
    subprocess.run([str(gpu_tool), str(fonts), str(tasks)], check=True)
if not a.native_only:
    descriptor = a.build / 'flash-inventory-reference/InventoryExporter-app.xml'
    if a.rebuild_reference or not descriptor.exists():
        subprocess.run(['python3', str(ROOT / 'tools/build_flash_reference.py'), 'InventoryExporter',
                        '--flash-root', str(a.flash_root.resolve()), '--output', str(descriptor.parent)], check=True)
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=120)
    layout = json.loads((directory / 'flash/potion-layout.json').read_text())
    assert len(layout) == len(cases), 'Every fixture must place a PotionInventoryView'
    for entry in layout:
        assert (entry['y'], entry['gridHeight'], entry['second']) == (468, 84, 88), f'Unexpected potion layout {entry}'
failures = []
for data in cases:
    filename = data['id'] + '.png'
    native = Image.open(directory / 'native' / filename).convert('RGBA')
    if gpu_tool.exists():
        gpu = Image.open(directory / 'native-gpu' / filename).convert('RGBA')
        assert native.tobytes() == gpu.tobytes(), f'Incremental GPU fixture {filename} differs'
    if not a.native_only:
        expected = Image.open(directory / 'flash' / filename).convert('RGBA')
        difference = ImageChops.difference(expected.crop((0, 300, 200, 500)), native.crop((0, 300, 200, 500)))
        if difference.getbbox(alpha_only=False):
            difference.save(directory / ('difference-' + filename))
            failures.append(dict(id=data['id'], potions=data['potions'], pixels=sum(any(pixel) for pixel in difference.get_flattened_data()),
                                 bounds=difference.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), classes=len(players),
    source_failures=failures, native_only=a.native_only), indent=2) + '\n')
assert not failures, f'{len(failures)} original potion fixtures differ; first failures: {failures[:10]}'
print(f'{len(cases)} potion widget states across {len(players)} classes match reconstruction and GPU uploads')
if not a.native_only:
    print('Potion widgets and the equipment/inventory region match original Flash exactly')
