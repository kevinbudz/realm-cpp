#!/usr/bin/env python3
"""Stats value clip-edge probe: right-column values ending in every digit at both reachable phases."""
import json, subprocess, sys
from pathlib import Path
from PIL import Image, ImageChops
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
d = ROOT / 'build/stats-clip-probe'; d.mkdir(parents=True, exist_ok=True)
assets = ROOT.parent / 'realm-client/src/kabam/rotmg/assets'
wizard = next(n for n in ET.parse(assets / 'EmbeddedData_PlayersCXML.dat').getroot() if n.get('id') == 'Wizard')
start = [int(v.strip(), 0) for v in wizard.findtext('Equipment').split(',')]
cases = []
values = []
for last in range(10):
    for prefix in ['', '1', '2', '11', '12', '21', '22', '3', '33']:
        values.append(int(prefix + str(last)))
for boost in [0, 4, -4, 14, 7, -7, 1, -1]:
    for value in values:
        stats = [dict(value=5, boost=0) for _ in range(6)]
        for i in (1, 3, 5):
            stats[i] = dict(value=value, boost=boost)
        cases.append(dict(id=str(len(cases)), type=int(wizard.get('type'), 0), mp=100, backpack=False, selected=1, tiers=True,
                          items=(start + [-1] * 20)[:20], potions=[1, 1], stats=stats))
tasks = d / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(d / 'flash'), art=str(ROOT / 'build/hud-art'), cases=cases)))
if 'native' not in sys.argv:
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), str(ROOT / 'build/flash-inventory-reference/InventoryExporter-app.xml'), '--', str(tasks)], check=True, timeout=600)
subprocess.run([str(ROOT / 'build/realm_hud_inputs'), str(ROOT / 'build/fonts-client'), str(tasks)], check=True)
metrics = {e['id']: e for e in json.loads((d / 'flash/stats-metrics.json').read_text())}
bad = []
for c in cases:
    f = Image.open(d / 'flash' / (c['id'] + '.png')).convert('RGBA'); n = Image.open(d / 'native' / (c['id'] + '.png')).convert('RGBA')
    diff = ImageChops.difference(f.crop((0, 373, 200, 500)), n.crop((0, 373, 200, 500)))
    if diff.getbbox(alpha_only=False):
        field = metrics[c['id']]['fields'][1]
        bad.append((c['stats'][1]['value'], c['stats'][1]['boost'], field['x'], field['valueWidth'], diff.getbbox(alpha_only=False)))
print(len(cases), 'cases', len(bad), 'differ')
for b in bad[:60]: print(b)
