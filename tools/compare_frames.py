#!/usr/bin/env python3
"""Compare matched Flash/native PNG frames without silently accepting differences.

Requires Pillow. The two frames must have identical dimensions, camera, time,
world state, settings, and UI. A successful comparison of an asset gallery is
not evidence of full gameplay parity.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops


def compare(reference, candidate, channel_tolerance=0):
    a, b = reference.convert('RGBA'), candidate.convert('RGBA')
    if a.size != b.size:
        raise ValueError(f'Frame dimensions differ: {a.size} vs {b.size}')
    diff = ImageChops.difference(a, b)
    histogram = diff.histogram()
    pixels = a.width * a.height
    total = sum(count * (i % 256) for i, count in enumerate(histogram))
    red, green, blue, alpha = diff.split()
    maximum_image = ImageChops.lighter(ImageChops.lighter(red, green), ImageChops.lighter(blue, alpha))
    changed = sum(maximum_image.histogram()[channel_tolerance + 1:])
    maximum = max(i % 256 for i, count in enumerate(histogram) if count)
    report = dict(width=a.width, height=a.height, different_pixels=changed,
                  total_pixels=pixels, different_fraction=changed / pixels,
                  mean_absolute_channel_error=total / (pixels * 4),
                  max_channel_error=maximum, channel_tolerance=channel_tolerance)
    return report, diff


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--output', type=Path, default=Path('captures/diff'))
    parser.add_argument('--channel-tolerance', type=int, default=0)
    parser.add_argument('--max-different-fraction', type=float, default=0)
    args = parser.parse_args()
    if not 0 <= args.channel_tolerance <= 255 or not 0 <= args.max_different_fraction <= 1:
        parser.error('Invalid comparison tolerance')
    report, diff = compare(Image.open(args.reference), Image.open(args.candidate), args.channel_tolerance)
    report['reference'] = str(args.reference)
    report['candidate'] = str(args.candidate)
    report['max_different_fraction'] = args.max_different_fraction
    report['passed'] = report['different_fraction'] <= args.max_different_fraction
    args.output.mkdir(parents=True, exist_ok=True)
    # Force an opaque alpha so RGB mismatches remain visible in image viewers.
    visible = diff.convert('RGB').point(lambda value: min(255, value * 8))
    visible.save(args.output / 'difference_x8.png')
    alpha = diff.getchannel('A').point(lambda value: min(255, value * 8))
    alpha.save(args.output / 'alpha_difference_x8.png')
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
