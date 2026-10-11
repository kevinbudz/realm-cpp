#!/usr/bin/env python3
"""Compares the native Options screen with Flash's own Options class (task 12-options).

The real ui/options/Options.as is built with AIR (tools/flash-reference/OptionsExporter.as), driven through each
tab, hover state and window size in tests/options_cases.json, and its frames are compared with the native screen
rendered by build/realm_options_inputs. Writes side-by-side images to build/options-reference.
Needs the AIR SDK and JDK used by tools/build_flash_reference.py, Pillow and numpy.
"""
import json, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/options-reference'
cases = json.loads((ROOT / 'tests/options_cases.json').read_text())
OUT.mkdir(parents=True, exist_ok=True)
tips = json.loads((ROOT / 'tests/options_tooltip_cases.json').read_text())
(OUT / 'flash-input.json').write_text(json.dumps(dict(output=str(OUT / 'flash'), cases=cases, tooltips=tips)))
subprocess.run(['python3', str(ROOT / 'tools/build_flash_reference.py'), 'OptionsExporter', '--output', str(OUT / 'exporter')], cwd=ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), '-nodebug', str(OUT / 'exporter/OptionsExporter-app.xml'), '--', str(OUT / 'flash-input.json')],
               cwd=ROOT, check=True, timeout=120, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.run([str(ROOT / 'build/realm_options_inputs'), str(ROOT / 'build/fonts-client'), str(ROOT.parent / 'realm-client'), str(ROOT / 'tests/options_cases.json'), str(OUT / 'native'), str(ROOT / 'tests/options_tooltip_cases.json')], check=True)
flash_metrics = json.loads((OUT / 'flash/metrics.json').read_text())
native_metrics = json.loads((OUT / 'native/metrics.json').read_text())
worst = []
for case, fm, nm in zip(cases, flash_metrics[:len(cases)], native_metrics[:len(cases)], strict=True):
    key = case['key']
    assert fm['key'] == nm['key'] == key
    assert fm['stage'] == [case['width'], case['height']], (key, 'window size', fm['stage'])
    assert fm['cells'] == nm['cells'], (key, 'grid cells', fm['cells'], nm['cells'])
    for i, (f, n) in enumerate(zip(fm['rects'], nm['rects'])):
        assert abs(f['x'] - n['x']) < 1e-6 and abs(f['y'] - n['y']) < 1e-6, (key, 'cell', i, f, n)
    if 'hover' in case and case['hover']['kind'] == 'label':
        assert nm['tooltip'], (key, 'native tooltip shown')
    f = np.asarray(Image.open(OUT / 'flash' / f'{key}.png').convert('RGB')).astype(float)
    n = np.asarray(Image.open(OUT / 'native' / f'{key}.png').convert('RGB')).astype(float)
    assert f.shape == n.shape, (key, f.shape, n.shape)
    diff = np.abs(f - n).max(2)
    mean = diff.mean(); big = (diff > 48).mean() * 100
    worst.append((mean, key))
    print(f'{key:22s} mean diff {mean:5.2f}/255   pixels differing by >48: {big:5.2f}%')
    h, w = f.shape[:2]
    pair = Image.new('RGB', (w * 2, h + 24), (80, 80, 80)); draw = ImageDraw.Draw(pair)
    for j, kind in enumerate(['flash', 'native']):
        pair.paste(Image.open(OUT / kind / f'{key}.png').convert('RGB'), (w * j, 24)); draw.text((w * j + 5, 5), f'{kind}: {key}', fill='white')
    pair.save(OUT / f'comparison-{key}.png')
    # Resized frames redraw text at drawable resolution, so glyph edges and 1 px placements differ more.
    base = f.shape[:2] == (600, 800)
    assert mean < (3.0 if base else 9.0) and big < (1.5 if base else 5.0), (key, 'native differs from Flash', mean, big)
flash_tips = flash_metrics[len(cases):]
native_tips = native_metrics[len(cases):]
for tip, fm, nm in zip(tips, flash_tips, native_tips, strict=True):
    key = tip['key']
    f = np.asarray(Image.open(OUT / 'flash' / f'{key}.png').convert('RGB')).astype(float)
    n = np.asarray(Image.open(OUT / 'native' / f'{key}.png').convert('RGB')).astype(float)
    diff = np.abs(f - n).max(2)
    # The frame size (content plus 12) is the layout contract; the pixels follow from it.
    # Flash's bounds include the outline stroke: one more than the frame.
    assert abs(fm['width'] - 1 - nm['frame_width']) <= .5 and abs(fm['height'] - 1 - nm['frame_height']) <= 1.5, (key, 'frame size', fm, nm)
    print(f'{key:22s} frame flash {fm["width"]:.0f}x{fm["height"]:.0f} native {nm["frame_width"]:.0f}x{nm["frame_height"]:.0f}   mean diff {diff.mean():5.2f}   >48: {(diff > 48).mean() * 100:5.2f}%')
    pair = Image.new('RGB', (600, 224), (80, 80, 80)); draw = ImageDraw.Draw(pair)
    for j, kind in enumerate(['flash', 'native']):
        pair.paste(Image.open(OUT / kind / f'{key}.png').convert('RGB'), (300 * j, 24)); draw.text((300 * j + 5, 5), f'{kind}: {key}', fill='white')
    pair.save(OUT / f'comparison-{key}.png')
    assert diff.mean() < 3.0 and (diff > 48).mean() * 100 < 2.5, (key, "tooltip differs from Flash")
print(f'{len(cases)} Flash/native Options frames and {len(tips)} tooltips compared; grid geometry identical')
