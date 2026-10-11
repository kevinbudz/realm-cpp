#!/usr/bin/env python3
"""Compare the nearby-container interaction panel with original InteractPanel/ContainerGrid."""
import argparse
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
directory = a.build / 'container-reference'
directory.mkdir(parents=True, exist_ok=True)
players = [node for node in ET.parse(a.flash_root / 'src/kabam/rotmg/assets/EmbeddedData_PlayersCXML.dat').getroot()
           if node.findtext('Class') == 'Player']
pool = sorted(int(record['key'][4:]) for record in json.loads((art / 'manifest.json').read_text())['images']
              if record['key'].startswith('item'))
LOOT_BAG, GRAVESTONE, VAULT_CHEST, GIFT_CHEST = 0x500, 0x502, 0x504, 0x744
cases = []
cursor = 0

def loot(count):
    global cursor
    items = [pool[(cursor + 37 * i) % len(pool)] for i in range(count)]
    cursor += 101
    return items + [-1] * (8 - count)

def case(player, container, selected=0, backpack=False, tiers=True, potions=None, **extra):
    start = [int(value.strip(), 0) for value in player.findtext('Equipment').split(',')]
    data = dict(id=str(len(cases)), type=int(player.attrib['type'], 0), mp=100, backpack=backpack,
                selected=selected, tiers=tiers, items=(start + [-1] * 20)[:20], **extra)
    if container:
        data['container'] = dict(type=container[0], items=container[1])
    if potions:
        data['potions'] = potions
    cases.append(data)

for player in players:
    bag = (LOOT_BAG, loot(8))
    case(player, bag)
    case(player, (LOOT_BAG, loot(8)), regions=[[0, 500, 200, 100]])
    case(player, (GRAVESTONE, loot(3)), tiers=False)
    case(player, (VAULT_CHEST, loot(5)), selected=2, backpack=True, potions=[2, 6])
    case(player, (VAULT_CHEST, cases[-1]['container']['items']), selected=0, backpack=True, potions=[2, 6],
         regions=[[0, 300, 200, 200]])
    case(player, (GIFT_CHEST, [-1] * 8), backpack=True, potions=[2, 6], regions=[[0, 500, 200, 100]])
    case(player, (GIFT_CHEST, [-1] * 8), backpack=True, potions=[2, 6], redraw=False)
    case(player, None, backpack=True, potions=[2, 6], regions=[[0, 500, 200, 100]])

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
    layout = json.loads((directory / 'flash/container-layout.json').read_text())
    assert len(layout) == sum('container' in data for data in cases), 'Every container fixture must place a ContainerGrid'
    for entry in layout:
        assert (entry['x'], entry['y']) == (14, 8), f'Unexpected container layout {entry}'
failures = []
for data in cases:
    filename = data['id'] + '.png'
    native = Image.open(directory / 'native' / filename).convert('RGBA')
    if gpu_tool.exists():
        gpu = Image.open(directory / 'native-gpu' / filename).convert('RGBA')
        assert native.tobytes() == gpu.tobytes(), f'Incremental GPU fixture {filename} differs'
    if not a.native_only:
        expected = Image.open(directory / 'flash' / filename).convert('RGBA')
        difference = ImageChops.difference(expected.crop((0, 300, 200, 600)), native.crop((0, 300, 200, 600)))
        if difference.getbbox(alpha_only=False):
            difference.save(directory / ('difference-' + filename))
            failures.append(dict(id=data['id'], container=data.get('container'),
                                 pixels=sum(any(pixel) for pixel in difference.get_flattened_data()),
                                 bounds=difference.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), classes=len(players),
    source_failures=failures, native_only=a.native_only), indent=2) + '\n')
assert not failures, f'{len(failures)} original container fixtures differ; first failures: {failures[:10]}'
print(f'{len(cases)} container panel states across {len(players)} classes match reconstruction and GPU uploads')
if not a.native_only:
    print('Container panels and the equipment/inventory region match original Flash exactly')
