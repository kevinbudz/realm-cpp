#!/usr/bin/env python3
"""Compare SellableObjectPanel (Merchant, ClosedVaultChest, GuildMerchant) with original InteractPanel."""
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
directory = a.build / 'sellable-reference'
directory.mkdir(parents=True, exist_ok=True)
assets = a.flash_root / 'src/kabam/rotmg/assets'
nodes = []
for path in sorted(assets.glob('EmbeddedData_*.dat')):
    try:
        nodes += list(ET.parse(path).getroot())
    except ET.ParseError:
        continue
by_id = {node.get('id'): node for node in nodes if node.get('id')}
kind = lambda name: int(by_id[name].get('type'), 0)
players = [node for node in nodes if node.findtext('Class') == 'Player']
guild_merchants = [node for node in nodes if node.findtext('Class') == 'GuildMerchant']
vaults = [node for node in nodes if node.findtext('Class') == 'ClosedVaultChest']
merchant = next(node for node in nodes if node.findtext('Class') == 'Merchant')
assert guild_merchants and vaults, 'Every SellableObject class needs a definition'
wizard = by_id['Wizard']
cases = []

def case(target, player=wizard, selected=0, extra=None, **sellable):
    for key, value in dict(stars=0, guildRank=-1, outstanding=False, hover=False).items():
        sellable.setdefault(key, value)
    start = [int(value.strip(), 0) for value in player.findtext('Equipment').split(',')]
    cases.append(dict(id=str(len(cases)), type=int(player.get('type'), 0), mp=100, backpack=False, selected=selected, tiers=True,
                      items=(start + [-1] * 20)[:20], potions=[1, 1], sellable=dict(type=int(target.get('type'), 0), **sellable), **(extra or {})))

# Short, two-line and three-line names; every currency; one- to ten-digit prices.
for name, price, currency in [('Masamune', 500, 0), ('Potion of Dexterity', 50, 1), ('Wand of Recompense', 5, 0),
                              ('Helm of the Great General', 12345, 1), ('Elemental Detonation Spell', 0, 2),
                              ('Seal of the Blessed Champion', 2147483647, 0), ('Dagger of Foul Malevolence', 1, 1),
                              ('Bloodsucker Skull', 99, 2), ('Potion of Life', 1000000, 0), ('Robe of the Grand Sorcerer', 777, 2)]:
    case(merchant, merchandise=kind(name), price=price, currency=currency)
    case(merchant, merchandise=kind(name), price=price, currency=currency, hover=True)
masamune = dict(merchandise=kind('Masamune'), price=500)
for currency in (-1, 0, 1, 2):
    case(merchant, currency=currency, outstanding=True, **masamune)
    case(merchant, currency=currency, outstanding=True, hover=True, **masamune)
case(merchant, currency=-1, **masamune)
for rank, stars in [(5, 0), (13, 2), (70, 69), (3, 3), (99, 0)]:
    case(merchant, currency=0, rankReq=rank, stars=stars, **masamune)
for vault in vaults:
    case(vault, price=750, currency=1)
    case(vault, price=750, currency=1, hover=True)
    case(vault)
for target in guild_merchants:
    case(target)
    case(target, guildRank=20)
    case(target, guildRank=30)
    case(target, guildRank=40, hover=True)
    case(target, guildRank=40, rankReq=8, stars=1)
for index, player in enumerate(players):
    # Values ending in a bare 4 hit the known bold-13 clip-edge coverage difference (docs/progress.md).
    stats = dict(stats=[dict(value=11 + 2 * index + 10 * stat, boost=0) for stat in range(6)]) if index % 2 else {}
    case(merchant, player=player, selected=index % 2, extra=stats, currency=index % 4 - 1, hover=index % 3 == 0,
         merchandise=kind('Potion of Dexterity'), price=25 * (index + 1))
case(merchant, currency=0, hover=True, **masamune)
case(merchant, currency=0, hover=True, extra=dict(redraw=False), **masamune)
case(merchant, currency=0, extra=dict(regions=[[0, 500, 200, 100]]), **masamune)
case(merchant, currency=0, hover=True, extra=dict(regions=[[0, 500, 200, 100]]), **masamune)

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
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=300)
    layout = json.loads((directory / 'flash/sellable-layout.json').read_text())
    assert len(layout) == len(cases), 'Every fixture must place a sellable panel'
    for entry, data in zip(layout, cases):
        sellable = data['sellable']
        buy = [child for child in entry['children'] if child['kind'] == 'buy']
        # HudState::player shows the button only when no rank or guild notice applies.
        blocked = sellable['stars'] < sellable.get('rankReq', 0) or (sellable['type'] in [int(n.get('type'), 0) for n in guild_merchants]
                                                                     and sellable['guildRank'] < 30)
        assert bool(buy) and buy[0]['visible'] != blocked or not buy and blocked, (entry, data)
        if buy and not blocked:
            assert buy[0]['enabled'] == (not sellable['outstanding']), (entry, data)
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
            failures.append(dict(id=data['id'], sellable=data['sellable'],
                                 pixels=sum(any(pixel) for pixel in difference.get_flattened_data()),
                                 bounds=difference.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cases=len(cases), source_failures=failures,
    native_only=a.native_only), indent=2) + '\n')
assert not failures, f'{len(failures)} of {len(cases)} original sellable panel fixtures differ; first failures: {failures[:10]}'
print(f'{len(cases)} sellable panel states match reconstruction and GPU uploads')
if not a.native_only:
    print('Sellable panels and the equipment/inventory region match original Flash exactly')
