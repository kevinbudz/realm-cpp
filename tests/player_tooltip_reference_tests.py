#!/usr/bin/env python3
"""Strict original-Flash/native PlayerToolTip comparisons on transparent RGBA.
AIR is used only to produce offline references; native runtime remains C++.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_flash_reference import build

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, default=ROOT / 'build/realm_player_tooltip_inputs')
parser.add_argument('--rebuild-reference', action='store_true')
parser.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
parser.add_argument('--java-home', type=Path, default=Path(os.environ.get('JAVA_HOME', str(Path.home() / 'Downloads/idea-IU-192.7142.36/jbr'))))
args = parser.parse_args()
output = ROOT / 'build/player-tooltip-reference'
reference = output / 'flash'
native = output / ('sanitize' if 'build-sanitize' in args.binary.parts else 'native')
reference.mkdir(parents=True, exist_ok=True)
inventory = json.loads((ROOT / 'build/generated/source_inventory.json').read_text())
source = Path(inventory['source_root'])
classes = [item for item in ET.parse(source / 'src/kabam/rotmg/assets/EmbeddedData_PlayersCXML.dat').getroot() if item.find('Player') is not None]
cases = []
for index, player in enumerate(classes):
    cap = lambda tag: int(player.find(tag).get('max'))
    base = dict(id=player.get('id'), type=int(player.get('type'), 0), name='Tank', level=20, stars=index * 5,
                guild='', guildRank=20, fellowGuild=False, hp=120, maxHp=200, hpBoost=0, hpCap=cap('MaxHitPoints'),
                mp=75, maxMp=100, mpBoost=0, mpCap=cap('MaxMagicPoints'),
                items=[int(item, 0) for item in player.findtext('Equipment').split(',')[:4]], datas=[-1] * 4)
    cases += [base, dict(base, id=base['id'] + '-guild', guild='Native Guild', fellowGuild=True, guildRank=(index % 5) * 10)]
base = next(case for case in cases if case['id'] == 'Knight')
cases += [dict(base, id='empty-name', name='', level=1),
          dict(base, id='long-name', name='WWWWWWWWWW', guild='WWWWWWWWWWWWWWWWWWWW', fellowGuild=True),
          dict(base, id='level-zero', name='NativeTest', level=0),
          dict(base, id='critical-hp', hp=10, mp=0),
          dict(base, id='zero-meters', hp=-10, maxHp=0, mp=-1, maxMp=0),
          dict(base, id='clamped-meters', hp=500, mp=500),
          dict(base, id='boosted', hp=800, maxHp=800, hpBoost=30, mp=252, maxMp=252),
          dict(base, id='negative-boost', hp=190, maxHp=190, hpBoost=-10, mp=90, maxMp=90, mpBoost=-10),
          dict(base, id='missing-rank', stars=-1),
          dict(base, id='full-rank', stars=70)]
tasks = output / 'tasks.json'
tasks.write_text(json.dumps(dict(output=str(reference), cases=cases)))
if args.rebuild_reference or not (reference / 'layout.json').exists():
    descriptor = build('PlayerTooltipExporter', source, args.sdk.resolve(), args.java_home.resolve(), ROOT / 'build/flash-player-tooltip-reference')
    subprocess.run([str(args.sdk / 'bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=180)
subprocess.run([str(args.binary.resolve()), str(ROOT / 'build/fonts-client'), str(ROOT / 'build/hud-art'), str(tasks), str(native)], check=True)
layouts = {item['id']: item for item in json.loads((reference / 'layout.json').read_text())}
sizes = {item['id']: item for item in json.loads((native / 'sizes.json').read_text())}
results = []
for case in cases:
    name = case['id']
    original = Image.open(reference / f'{name}.png').convert('RGBA')
    actual = Image.open(native / f'{name}.png').convert('RGBA')
    diff = ImageChops.difference(original, actual)
    count = sum(pixel != (0, 0, 0, 0) for pixel in diff.get_flattened_data())
    size_ok = sizes[name]['width'] == layouts[name]['contentWidth'] + 32 and sizes[name]['height'] == layouts[name]['contentHeight'] + 32
    results.append(dict(id=name, differing_pixels=count, size_matches=size_ok))
    if count or not size_ok:
        diff.save(native / f'{name}-diff.png')
        print(f'{name}: {count} differing pixels; sizes match: {size_ok}')
(output / 'comparison.json').write_text(json.dumps(results, indent=2) + '\n')
failed = sum(item['differing_pixels'] != 0 or not item['size_matches'] for item in results)
print(f'{len(cases) - failed}/{len(cases)} PlayerToolTip fixtures match Flash exactly')
raise SystemExit(1 if failed else 0)
