#!/usr/bin/env python3
"""Compare ui/deca/*.svg with the Flash atlas frames they redraw.

Each SVG is rendered by realm_vector_render at its natural size and 1 px per unit, and compared
with the frame of the same name from tools/extract_deca_ui.py. A vector redraw is not meant to be
pixel-identical (45-degree pixel stairs become straight, antialiased edges), so the error is a
guide, not a pass mark. The tool does fail when an SVG does not render, its natural size differs
from the frame, or its data-stretch-x / data-stretch-y band differs from the frame's slice
rectangle in slices.json.

Usage: compare_deca_ui.py --atlas DIR --renderer build/tests/realm_vector_render
       [--svg-dir ui/deca] [--output DIR] [--zoom 6] [NAME ...]
With --output, writes NAME.png per frame: the Flash frame, the vector render at 1x, their
difference (x4), and the vector rendered natively at --zoom density beside the frame enlarged
with nearest-neighbour. Requires Pillow.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent


def flatten(image: Image.Image, backdrop) -> Image.Image:
    base = Image.new("RGBA", image.size, backdrop + (255,))
    return Image.alpha_composite(base, image.convert("RGBA")).convert("RGB")


def mean_error(a: Image.Image, b: Image.Image) -> float:
    """Mean absolute channel difference (0-255), over black and over white backdrops."""
    total = 0
    for backdrop in ((0, 0, 0), (255, 255, 255)):
        diff = ImageChops.difference(flatten(a, backdrop), flatten(b, backdrop))
        histogram = diff.histogram()
        total += sum((i % 256) * count for i, count in enumerate(histogram))
    return total / (2 * 3 * a.width * a.height)


# Frames whose code slices them with its own rectangle instead of slices.json:
# TabContentBackground.addDecor uses (21, 0, 1, 14) for the left decor and (18, 0, 1, 14) for the
# others (their slices.json rectangles lie outside the frame), and TabContentBackground itself
# (15, 15, 1, 1).
CODE_SLICES = {
    "tab_open_decor_left.png": {"type": "3grid", "rectangle": {"x": 21, "y": 0, "w": 1, "h": 14}},
    "tab_open_decor_center.png": {"type": "3grid", "rectangle": {"x": 18, "y": 0, "w": 1, "h": 14}},
    "tab_open_decor_right.png": {"type": "3grid", "rectangle": {"x": 18, "y": 0, "w": 1, "h": 14}},
}


def expected_band(slice_entry, axis: str):
    if not slice_entry:
        return None
    rect = slice_entry["rectangle"]
    kind = slice_entry["type"]
    if kind == "9grid" or (kind == "3grid" and ((axis == "x") == (rect["y"] == 0))):
        start = rect[axis]
        return (start, start + rect["w" if axis == "x" else "h"])
    return None


def svg_band(text: str, axis: str):
    match = re.search(rf'data-stretch-{axis}="([^"]*)"', text)
    if not match:
        return None
    start, end = (float(v) for v in match.group(1).replace(",", " ").split())
    return (start, end)


def render(renderer: Path, svg: Path, out: Path, width=0, height=0, scale=1.0, *flags) -> None:
    command = [str(renderer), str(svg), str(out), str(width), str(height), str(scale), *flags]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"exit {result.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--atlas", type=Path, required=True, help="extract_deca_ui.py output")
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--svg-dir", type=Path, default=ROOT / "ui" / "deca")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--zoom", type=int, default=6)
    parser.add_argument("names", nargs="*")
    args = parser.parse_args()

    frames = json.loads((args.atlas / "frames.json").read_text())["frames"]
    slices = json.loads((args.atlas / "slices.json").read_text())["slices"]
    atlas = Image.open(args.atlas / "atlas.png").convert("RGBA")
    svgs = sorted(args.svg_dir.glob("*.svg"))
    if args.names:
        svgs = [s for s in svgs if s.stem in args.names]
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)

    failures = 0
    rows = []
    with tempfile.TemporaryDirectory() as scratch:
        for svg in svgs:
            name = svg.stem
            key = name + ".png"
            if key not in frames:
                print(f"{name:42s} (no atlas frame; skipped)")
                continue
            box = frames[key]["frame"]
            original = atlas.crop((box["x"], box["y"], box["x"] + box["w"], box["y"] + box["h"]))
            text = svg.read_text()
            problems = []
            for axis in ("x", "y"):
                want = expected_band(CODE_SLICES.get(key, slices.get(key)), axis)
                have = svg_band(text, axis)
                same = (want is None and have is None) or (
                    want is not None and have is not None
                    and all(abs(w - h) < 1e-6 for w, h in zip(want, have)))
                if not same:
                    problems.append(f"stretch-{axis} {have} != slice {want}")
            out = Path(scratch) / key
            try:
                render(args.renderer, svg, out)
            except RuntimeError as failure:
                problems.append(str(failure))
                vector = None
            else:
                vector = Image.open(out).convert("RGBA")
                if vector.size != original.size:
                    problems.append(f"size {vector.size} != frame {original.size}")
            score = mean_error(original, vector) if vector and not problems else float("nan")
            rows.append((name, score, problems))
            failures += bool(problems)
            print(f"{name:42s} {score:6.2f}  {'; '.join(problems)}")
            if args.output and vector and vector.size == original.size:
                zoom = args.zoom
                big = Path(scratch) / f"{name}@{zoom}.png"
                render(args.renderer, svg, big, 0, 0, zoom)
                native = Image.open(big).convert("RGBA")
                diff = ImageChops.difference(flatten(original, (0, 0, 0)),
                                             flatten(vector, (0, 0, 0)))
                diff = diff.point(lambda v: min(255, v * 4))
                w, h = original.size
                panel = Image.new("RGBA", (w * zoom * 5 + 40, h * zoom), (255, 0, 255, 255))
                for i, image in enumerate((original, vector, diff.convert("RGBA"))):
                    panel.alpha_composite(image.resize((w * zoom, h * zoom), Image.NEAREST),
                                          (i * (w * zoom + 10), 0))
                panel.alpha_composite(native, (3 * (w * zoom + 10), 0))
                checker = Image.new("RGBA", native.size, (40, 40, 40, 255))
                panel.alpha_composite(Image.alpha_composite(checker, native),
                                      (4 * (w * zoom + 10), 0))
                panel.save(args.output / key)

    scored = [score for _, score, problems in rows if not problems]
    if scored:
        print(f"\n{len(rows)} frames, mean error {sum(scored) / len(scored):.2f} "
              f"(0-255 per channel over black and white), {failures} failing")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
