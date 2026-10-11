#!/usr/bin/env python3
"""Compare the stats tab with original StatsView/StatView and retained GPU uploads."""
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
directory = a.build / 'stats-reference'
directory.mkdir(parents=True, exist_ok=True)
players = [node for node in ET.parse(a.flash_root / 'src/kabam/rotmg/assets/EmbeddedData_PlayersCXML.dat').getroot()
           if node.findtext('Class') == 'Player']
names = ('Attack', 'Defense', 'Speed', 'Dexterity', 'HpRegen', 'MpRegen')
cases = []

def case(player, values, boosts, selected=1, backpack=False):
    start = [int(value.strip(), 0) for value in player.findtext('Equipment').split(',')]
    cases.append(dict(id=str(len(cases)), type=int(player.attrib['type'], 0), mp=100,
                      backpack=backpack, selected=selected, tiers=True,
                      items=(start + [-1] * 20)[:20],
                      stats=[dict(value=v, boost=b) for v, b in zip(values, boosts)]))

for player in players:
    base = [int(player.findtext(name)) for name in names]
    caps = [int(player.find(name).attrib['max']) for name in names]
    case(player, base, [0] * 6)
    case(player, caps, [0] * 6)
    cases[-1]['regions'] = [[0, 373, 200, 127]]
    case(player, [cap + 10 for cap in caps], [10] * 6, backpack=True)
    case(player, [0] * 6, [0] * 6)
    case(player, [value - 5 for value in base], [-5] * 6)
    cases[-1]['regions'] = [[0, 373, 200, 127]]
    case(player, [1, 10, 100, 11, 99, 25], [1, 0, 5, -1, 9, -5])
    cases[-1]['regions'] = [[0, 373, 200, 127]]
    # Return from both inventory grids and change stats while they are hidden.
    case(player, base, [0] * 6, selected=0, backpack=True)
    case(player, caps, [0] * 6, selected=0, backpack=True)
    cases[-1]['redraw'] = False
    case(player, caps, [0] * 6, selected=2, backpack=True)
    case(player, caps, [-1] * 6, backpack=True)

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
failures = []
for data in cases:
    filename = data['id'] + '.png'
    native = Image.open(directory / 'native' / filename).convert('RGBA')
    if gpu_tool.exists():
        gpu = Image.open(directory / 'native-gpu' / filename).convert('RGBA')
        assert native.tobytes() == gpu.tobytes(), f'Incremental GPU fixture {filename} differs'
    if not a.native_only:
        expected = Image.open(directory / 'flash' / filename).convert('RGBA')
        difference = ImageChops.difference(expected.crop((0,300,200,500)), native.crop((0,300,200,500)))
        if difference.getbbox(alpha_only=False):
            difference.save(directory / ('difference-' + filename))
            failures.append(dict(id=data['id'], pixels=sum(any(pixel) for pixel in difference.get_flattened_data()),
                                 bounds=difference.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), classes=len(players),
    source_failures=failures, native_only=a.native_only), indent=2) + '\n')
assert not failures, f'{len(failures)} original stats tab fixtures differ; first failures: {failures[:10]}'
print(f'{len(cases)} stats/tab transitions across {len(players)} classes match reconstruction and GPU uploads')
if not a.native_only:
    print('Stats and equipment/inventory regions match original Flash exactly')
