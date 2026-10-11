#!/usr/bin/env python3
"""Compare entire transparent fields, including Flash's shadow alpha."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fonts', type=Path)
parser.add_argument('--renderer', type=Path, default=Path(__file__).resolve().parents[1] / 'build/realm_font_inputs')
parser.add_argument('--styles', nargs='+')
parser.add_argument('--samples', type=int, nargs='+')
parser.add_argument('--stress', action='store_true')
args = parser.parse_args()
args.fonts = args.fonts.resolve()
command = [str(args.renderer.resolve()), str(args.fonts)]
if args.stress:
    command.append('--stress')
subprocess.run(command, check=True, timeout=120)
tasks = json.loads((args.fonts / 'tasks.json').read_text())
failures = []
count = 0
for style in tasks['fonts']:
    if args.styles is not None and style['id'] not in args.styles:
        continue
    for index, sample in enumerate(tasks['samples']):
        if args.samples is not None and index not in args.samples:
            continue
        relative = Path(style['id']) / f'{index}.png'
        with Image.open(args.fonts / 'transparent' / relative) as expected, Image.open(args.fonts / 'native-transparent' / relative) as native:
            if expected.tobytes() != native.tobytes():
                difference = ImageChops.difference(expected, native)
                difference.save(args.fonts / 'native-transparent' / relative.with_name(f'{index}-difference.png'))
                failures.append((style['id'], sample, difference.getbbox(alpha_only=False)))
        count += 1
if count == 0:
    raise AssertionError('No reference fields selected')
if failures:
    raise AssertionError(f'{len(failures)} of {count} fields differ: {failures}')
print(f'{count} transparent SimpleText fields match native rasterization exactly')
