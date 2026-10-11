#!/usr/bin/env python3
"""Compare the nearby-portal interaction panel with original InteractPanel/PortalPanel."""
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
directory = a.build / 'portal-reference'
directory.mkdir(parents=True, exist_ok=True)
assets = a.flash_root / 'src/kabam/rotmg/assets'
wizard = next(node for node in ET.parse(assets / 'EmbeddedData_PlayersCXML.dat').getroot() if node.get('id') == 'Wizard')
portals = []
for path in sorted(assets.glob('EmbeddedData_*.dat')):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        continue
    portals += [node for node in root if node.findtext('Class') == 'Portal']
start = [int(value.strip(), 0) for value in wizard.findtext('Equipment').split(',')]
cases = []

def case(portal, name='', active=True, hover=False, **extra):
    cases.append(dict(id=str(len(cases)), type=int(wizard.get('type'), 0), mp=100, backpack=False, selected=0, tiers=True,
                      items=(start + [-1] * 20)[:20], potions=[1, 1],
                      portal=dict(type=int(portal.get('type'), 0), name=name, active=active, hover=hover), **extra))

realm = next(node for node in portals if node.get('id') == 'Nexus Portal')
locked = next(node for node in portals if node.findtext('LockedPortal') is not None)
for portal in portals:
    case(portal)
# Server names: PortalMonitor realm names with player counts, dungeon display IDs and long wrapped names.
for name in ['Medusa (0)', 'Djinn (12)', 'Beholder (85)', 'Ent Ancient (7)', 'Lich (1)', 'Abyss of Demons', 'Ww', 'i', 'W',
             'Haunted Cemetery Final Rest Portal Extended', 'AVeryLongSingleWordWithoutSpacesThatCannotWrap',
             'Two  Spaces', 'Haunted Cemetery Final Rest Portal of the Ancient Kings Beyond the Sea', 'Trailing ', ' Leading', "Oryx's Castle", 'Pirate Cave Portal of the Seven Seas', '(0)', '']:
    case(realm, name)
    case(realm, name, active=False)
case(realm, 'Medusa (3)', hover=True)
case(realm, 'Medusa (3)', hover=True, redraw=False)
case(realm, 'Medusa (4)', hover=True, regions=[[0, 500, 200, 100]])
case(realm, 'Medusa (4)', regions=[[0, 500, 200, 100]])
case(locked, 'Locked Wine Cellar Portal')
case(locked, 'Locked Wine Cellar Portal', active=False)
case(locked, 'Wine Cellar Portal')
case(locked, 'Locked Locked Portal')
case(locked, 'Locked ')

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
    layout = json.loads((directory / 'flash/portal-layout.json').read_text())
    assert len(layout) == len(cases), 'Every fixture must place a PortalPanel'
    for entry, data in zip(layout, cases):
        locked_type = data['portal']['type'] == int(locked.get('type'), 0)
        shown = data['portal']['active'] and not locked_type
        assert (entry['button'] is not None) == shown, f'Unexpected Enter button visibility {entry}'
        if shown:
            # HudInput::portal_button_at uses this TextButton rectangle.
            assert (entry['button']['x'], entry['button']['y'], entry['button']['shapeWidth'], entry['button']['height']) == (73, 562, 54, 26), entry
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
            failures.append(dict(id=data['id'], portal=data['portal'],
                                 pixels=sum(any(pixel) for pixel in difference.get_flattened_data()),
                                 bounds=difference.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), portals=len(portals),
    source_failures=failures, native_only=a.native_only), indent=2) + '\n')
assert not failures, f'{len(failures)} of {len(cases)} original portal fixtures differ; first failures: {failures[:10]}'
print(f'{len(cases)} portal panel states across {len(portals)} portal types match reconstruction and GPU uploads')
if not a.native_only:
    print('Portal panels and the equipment/inventory region match original Flash exactly')
