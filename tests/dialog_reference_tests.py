#!/usr/bin/env python3
"""Export real Flash/native Dialogs, check layout, and prepare visual pairs.

Pixel equality is intentionally not a gate: review the generated comparisons.
"""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rebuild-reference', action='store_true')
args = parser.parse_args()
output = ROOT / 'build/dialog-reference'
output.mkdir(parents=True, exist_ok=True)
cases = json.loads((ROOT / 'tests/dialog_cases.json').read_text())
inputs = output / 'flash-input.json'
inputs.write_text(json.dumps({'output': str(output / 'flash'), 'cases': cases}))
if args.rebuild_reference or not (output / 'flash/metrics.json').exists():
    subprocess.run(['python3', str(ROOT / 'tools/build_flash_reference.py'), 'DialogExporter',
                    '--output', str(output / 'exporter')], cwd=ROOT, check=True)
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), '-nodebug',
                    str(output / 'exporter/DialogExporter-app.xml'), '--', str(inputs)],
                   cwd=ROOT, check=True, timeout=45)
subprocess.run([str(ROOT / 'build/realm_dialog_inputs'), str(ROOT / 'build/fonts-client'),
                str(ROOT.parent / 'realm-client'), str(ROOT / 'tests/dialog_cases.json'),
                str(output / 'native')], cwd=ROOT, check=True)
flash = json.loads((output / 'flash/metrics.json').read_text())
native = json.loads((output / 'native/metrics.json').read_text())
for a, b in zip(flash, native, strict=True):
    assert a['key'] == b['key'] and a['height'] == b['height'], (a, b)
    if a['buttonY'] >= 0:
        assert a['buttonY'] == b['buttonY'], (a, b)

keys = [c['key'] for c in cases if c['width'] == 800]
base = Image.new('RGB', (720, len(keys)*270), (80, 80, 80))
draw = ImageDraw.Draw(base)
for i, key in enumerate(keys):
    h = next(a['height'] for a in flash if a['key'] == key)
    y = (600-h-1)/2
    for j, kind in enumerate(['flash', 'native']):
        im = Image.open(output / kind / (key+'.png')).convert('RGB').crop((220, int(y-20), 580, int(y+h+25)))
        base.paste(im, (j*360, i*270+22))
        draw.text((j*360+10, i*270+4), f'{kind}: {key}', fill='white')
base.save(output / 'comparison-base.png')
for c in cases:
    w, h, key = c['width'], c['height'], c['key']
    if w == 800:
        continue
    s = h/600
    x, y = (w-301*s)/2, (h-101*s)/2
    pair = Image.new('RGB', (int(340*s)*2, int(150*s)+24), (80, 80, 80))
    draw = ImageDraw.Draw(pair)
    for j, kind in enumerate(['flash', 'native']):
        im = Image.open(output / kind / (key+'.png')).convert('RGB').crop((int(x-20*s), int(y-20*s), int(x+320*s), int(y+130*s)))
        pair.paste(im, (int(340*s)*j, 24))
        draw.text((int(340*s)*j+5, 5), f'{kind} {w}x{h}', fill='white')
    pair.save(output / f'comparison-{w}.png')
print(f'All {len(cases)} dialog layout metrics match real Flash; visual pairs saved to {output}')
