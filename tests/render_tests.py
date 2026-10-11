#!/usr/bin/env python3
"""Verify native nearest-sampled galleries against the actual source PNG crops.

Requires Pillow and an SDL/OpenGL display. This tests the image import, atlas,
GPU sampling, alpha composition, and capture path; it does not certify Flash
gameplay fidelity. Run manually, so headless core builds need no GPU or Pillow.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
from PIL import Image

base = Path(__file__).parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('viewer', type=Path)
    parser.add_argument('--flash-root', type=Path, default=base / '../realm-client')
    parser.add_argument('--output', type=Path, default=base / 'captures/render-tests')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = args.flash_root.resolve()
    data = module('importer', base / 'tools/import_flash.py').scan(root)
    compare = module('compare', base / 'tools/compare_frames.py').compare
    for name in ['lofiEnvironment', 'lofiEnvironment2', 'lofiChar16x8', 'textile5x5', 'lofiObj40x40', 'invisible']:
        sheet = next(s for s in data['sheets'] if s['name'] == name)
        capture = args.output / (name + '.png')
        subprocess.run([str(args.viewer.resolve()), '--flash-root', str(root), '--sheet', name,
                        '--hidden', '--benchmark', '--frames', '1', '--capture', str(capture)], check=True,
                       stdout=subprocess.PIPE, text=True)
        expected = Image.new('RGBA', (800, 600), (0, 0, 0, 255))
        if sheet['asset'] >= 0:
            source = Image.open(root / data['assets'][sheet['asset']]['path']).convert('RGBA')
            # BitmapData PNG decode quantization followed by premultiplied
            # Stage3D texture upload and SOURCE_ALPHA blending.
            pixels = bytearray(source.tobytes())
            for at in range(0, len(pixels), 4):
                alpha = pixels[at + 3]
                for channel in range(3):
                    value = pixels[at + channel]
                    if alpha != 255:
                        value = (((value * alpha) >> 8) << 8) // alpha if alpha else 0
                    pixels[at + channel] = value if alpha == 255 else (value * alpha + 255) >> 8
            source = Image.frombytes('RGBA', source.size, bytes(pixels))
            tile_w, tile_h = sheet['width'], sheet['height']
            cell_w, cell_h = tile_w * 4 + 8, tile_h * 4 + 8
            cols, rows = max(1, 784 // cell_w), max(1, 584 // cell_h)
            sheet_cols = source.width // tile_w
            for i in range(min(cols * rows, sheet['count'])):
                x, y = i % sheet_cols * tile_w, i // sheet_cols * tile_h
                sprite = source.crop((x, y, x + tile_w, y + tile_h)).resize((tile_w * 4, tile_h * 4), Image.Resampling.NEAREST)
                expected.alpha_composite(sprite, (8 + i % cols * cell_w, 8 + i // cols * cell_h))
        report, _ = compare(expected, Image.open(capture))
        assert report['different_pixels'] == 0, (name, report)
        print(f"{name}: all {report['total_pixels']} framebuffer pixels match source PNG reference exactly")


if __name__ == '__main__':
    main()
