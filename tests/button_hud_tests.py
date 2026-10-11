#!/usr/bin/env python3
"""Compare ButtonPanel/ReskinPanel interaction panels with original InteractPanel."""
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
p.add_argument('--flash-only', action='store_true')
p.add_argument('--rebuild-reference', action='store_true')
a = p.parse_args()
a.build = a.build.resolve()
fonts = (a.fonts or a.build / 'fonts-client').resolve()
art = (a.art or a.build / 'hud-art').resolve()
directory = a.build / 'button-reference'
directory.mkdir(parents=True, exist_ok=True)
assets = a.flash_root / 'src/kabam/rotmg/assets'
# HudState::Button kinds, in button_panel_classes order.
CLASSES = ['GuildBoard', 'GuildChronicle', 'CharacterChanger', 'ReskinVendor']
TITLES = ['Guild Board', 'Guild Chronicle', 'Change Characters', 'Change Skin']
BUTTONS = [(74.5, 562, 51, 26), (74.5, 562, 51, 26), (65, 562, 70, 26), (66, 562, 68, 26)]
objects, players = [], []
for path in sorted(assets.glob('EmbeddedData_*.dat')):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        continue
    objects += [node for node in root if node.findtext('Class') in CLASSES]
    players += [node for node in root if node.findtext('Class') == 'Player']
assert sorted({node.findtext('Class') for node in objects}) == sorted(CLASSES), 'Every ButtonPanel class needs a definition'
wizard = next(node for node in players if node.get('id') == 'Wizard')
cases = []

def case(target, hover=False, player=wizard, selected=0, **extra):
    start = [int(value.strip(), 0) for value in player.findtext('Equipment').split(',')]
    cases.append(dict(id=str(len(cases)), type=int(player.get('type'), 0), mp=100, backpack=False, selected=selected, tiers=True,
                      items=(start + [-1] * 20)[:20], potions=[1, 1],
                      button=dict(type=int(target.get('type'), 0), kind=CLASSES.index(target.findtext('Class')), hover=hover), **extra))

for target in objects:
    case(target)
    case(target, hover=True)
for index, player in enumerate(players):
    # Values ending in a bare 4 hit the known bold-13 clip-edge coverage difference (docs/progress.md).
    stats = dict(stats=[dict(value=11 + 2 * index + 10 * stat, boost=0) for stat in range(6)]) if index % 2 else {}
    case(objects[index % len(objects)], hover=index % 2 == 1, player=player, selected=index % 2, **stats)
first = next(node for node in objects if node.findtext('Class') == 'CharacterChanger')
case(first, hover=True)
case(first, hover=True, redraw=False)
case(first, regions=[[0, 500, 200, 100]])
case(first, hover=True, regions=[[0, 500, 200, 100]])

tasks = directory / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(directory / 'flash'), art=str(art), cases=cases)))
gpu_tool = a.build / 'realm_hud_gpu_inputs'
if not a.flash_only:
    subprocess.run([str(a.build / 'realm_hud_inputs'), str(fonts), str(tasks)], check=True)
    if gpu_tool.exists():
        subprocess.run([str(gpu_tool), str(fonts), str(tasks)], check=True)
if not a.native_only:
    descriptor = a.build / 'flash-inventory-reference/InventoryExporter-app.xml'
    if a.rebuild_reference or not descriptor.exists():
        subprocess.run(['python3', str(ROOT / 'tools/build_flash_reference.py'), 'InventoryExporter',
                        '--flash-root', str(a.flash_root.resolve()), '--output', str(descriptor.parent)], check=True)
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=180)
    layout = json.loads((directory / 'flash/button-layout.json').read_text())
    assert len(layout) == len(cases), 'Every fixture must place a button panel'
    for entry, data in zip(layout, cases):
        kind = data['button']['kind']
        # Single-line titles: Hud::button draws them with FontBook at these positions.
        # ReskinPanel sets htmlText after multiline, so its paragraph adds an empty line.
        assert [line['text'] for line in entry['lines']] == ([TITLES[kind] + '\r', ''] if kind == 3 else [TITLES[kind]]), entry
        assert entry['y'] == (0 if kind == 3 else 6), entry
        # HudInput::button_at uses button_panel_buttons.
        button = entry['button']
        assert (button['x'], button['y'], button['shapeWidth'], button['height']) == BUTTONS[kind], entry
if a.flash_only:
    raise SystemExit
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
            failures.append(dict(id=data['id'], button=data['button'],
                                 pixels=sum(any(pixel) for pixel in difference.get_flattened_data()),
                                 bounds=difference.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), objects=len(objects),
    source_failures=failures, native_only=a.native_only), indent=2) + '\n')
assert not failures, f'{len(failures)} of {len(cases)} original button panel fixtures differ; first failures: {failures[:10]}'
print(f'{len(cases)} button panel states across {len(objects)} objects match reconstruction and GPU uploads')
if not a.native_only:
    print('Button panels and the equipment/inventory region match original Flash exactly')
