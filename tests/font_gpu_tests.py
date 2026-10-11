#!/usr/bin/env python3
"""Compare native text with complete fields rasterized by original SimpleText."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageChops

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('fonts', type=Path)
p.add_argument('--viewer', type=Path, default=Path(__file__).resolve().parents[1] / 'build/realm_viewer')
p.add_argument('--styles', nargs='+')
p.add_argument('--samples', type=int, nargs='+')
a = p.parse_args(); a.fonts = a.fonts.resolve(); a.viewer = a.viewer.resolve()
tasks = json.loads((a.fonts / 'tasks.json').read_text())
styles = a.styles or [font['id'] for font in tasks['fonts']]
failures = []; count = 0
for style in styles:
    parts = style.split('-')
    size, bold, shadow = map(int, parts[:3])
    for index, sample in enumerate(tasks['samples']):
        if a.samples is not None and index not in a.samples:
            continue
        capture = a.fonts / 'native' / style / f'{index}.png'
        command = [str(a.viewer), '--fonts', str(a.fonts), '--text', sample, '--font-size', str(size), '--hidden', '--frames', '1', '--capture', str(capture)]
        if bold:
            command.append('--font-bold')
        if shadow:
            command.append('--font-shadow')
        if parts[3:] == ['yhalf']:
            command.append('--font-yhalf')
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, timeout=10)
        with Image.open(a.fonts / 'references' / style / f'{index}.png') as source, Image.open(capture) as native:
            expected = source.convert('RGBA'); native = native.convert('RGBA')
            if expected.size != (800, 600):
                # Earlier development packs contain a field rectangle only.
                native = native.crop((8, 8, 8 + expected.width, 8 + expected.height))
                pixels = []
                for r, g, b, alpha in expected.get_flattened_data():
                    pixels.append(tuple(value if alpha == 255 else (value * alpha + 255) >> 8 for value in (r, g, b)) + (255,))
                expected.putdata(pixels)
            if expected.tobytes() != native.tobytes():
                difference = ImageChops.difference(expected, native)
                difference.save(capture.with_name(f'{index}-difference.png'))
                failures.append((style, sample, difference.convert('RGB').getbbox()))
        count += 1
if failures:
    raise AssertionError(f'Flash/native text differs: {failures}')
print(f'{count} original SimpleText fields match native text exactly')
