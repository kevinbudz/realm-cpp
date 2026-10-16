#!/usr/bin/env python3
"""Trace a Deca UI atlas frame into a ui/deca SVG.

A starting point for redrawing an ornate frame, not a converter to trust blindly: check the result
with tools/compare_deca_ui.py and by eye. Pixels are grouped into a small palette; each colour
becomes one even-odd path traced on the pixel lattice (diagonally touching pixels join, so a 1 px
diagonal line becomes one shape); runs of 45-degree and 2:1 pixel stairs become the straight line
through their midpoints. Vertices on the stretch band's edges stay put, so a band stretches flat as
Flash's scale-9 does. Translucent colours are drawn first, then opaque ones by area, each filling
its own pixels and those of the layers drawn over it, so no seams show between layers.

By default each straightened stair is extended to meet the edges beside it; --jog keeps the half-
pixel step at its ends instead, which suits tiny shapes (it loses less area).

Usage: trace_deca_ui.py --atlas DIR [--output ui/deca] [--jog] [--note TEXT] NAME [NAME ...]
DIR is tools/extract_deca_ui.py output. Requires Pillow.
"""
import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
# Frames TabContentBackground slices with its own rectangle (see tools/compare_deca_ui.py).
CODE_SLICES = {
    'tab_open_decor_left': (21, 22),
    'tab_open_decor_center': (18, 19),
    'tab_open_decor_right': (18, 19),
}


def f(v):
    s = f'{v:.3f}'.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def bands(atlas, name):
    if name in CODE_SLICES:
        return CODE_SLICES[name], None
    entry = json.loads((atlas / 'slices.json').read_text())['slices'].get(name + '.png')
    if not entry:
        return None, None
    r = entry['rectangle']
    if entry['type'] == '9grid':
        return (r['x'], r['x'] + r['w']), (r['y'], r['y'] + r['h'])
    return ((r['x'], r['x'] + r['w']), None) if r['y'] == 0 else (None, (r['y'], r['y'] + r['h']))


def palette(px, w, h, tol, alpha_min, min_count):
    counts = Counter(px[x, y] for y in range(h) for x in range(w) if px[x, y][3] >= alpha_min)
    reps = []
    for c, n in counts.most_common():
        for r in reps:
            if max(abs(c[k] - r[0][k]) for k in range(4)) <= tol:
                r[1] += n
                break
        else:
            reps.append([c, n])
    major = [r for r in reps if r[1] >= min_count] or reps[:1]
    return [tuple(r[0]) for r in major]


def nearest(c, colors):
    def dist(r):
        # premultiplied distance, so a faint pixel matches a faint color
        return sum(abs(c[k] * c[3] - r[k] * r[3]) for k in range(3)) / 255 + abs(c[3] - r[3])
    return min(range(len(colors)), key=lambda i: dist(colors[i]))


def contours(mask, w, h):
    """Closed lattice loops around the mask, interior on the right (y down)."""
    edges = defaultdict(list)
    inside = lambda x, y: 0 <= x < w and 0 <= y < h and mask[y][x]
    for y in range(h):
        for x in range(w):
            if not mask[y][x]:
                continue
            if not inside(x, y - 1):
                edges[(x, y)].append((x + 1, y))
            if not inside(x + 1, y):
                edges[(x + 1, y)].append((x + 1, y + 1))
            if not inside(x, y + 1):
                edges[(x + 1, y + 1)].append((x, y + 1))
            if not inside(x - 1, y):
                edges[(x, y + 1)].append((x, y))
    loops = []
    while edges:
        start = next(iter(edges))
        loop = [start]
        prev = None
        cur = start
        while True:
            outs = edges[cur]
            if len(outs) > 1 and prev is not None:
                # At a diagonal touch, turn left: pixels that touch at a corner join one contour
                # (8-connected), so a 1-px diagonal line becomes one stair, then one straight band.
                dx, dy = cur[0] - prev[0], cur[1] - prev[1]
                left = (cur[0] + dy, cur[1] - dx)
                nxt = left if left in outs else outs[0]
            else:
                nxt = outs[0]
            outs.remove(nxt)
            if not outs:
                del edges[cur]
            prev, cur = cur, nxt
            if cur == start:
                break
            loop.append(cur)
        loops.append(loop)
    return loops


def merge_collinear(loop):
    out = []
    n = len(loop)
    for i in range(n):
        a, b, c = loop[i - 1], loop[i], loop[(i + 1) % n]
        if (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]) != 0:
            out.append(b)
    return out


def simplify(loop, sx, sy, extend):
    pts = merge_collinear(loop)
    n = len(pts)
    if n < 4:
        return pts
    fixed_x = set(sx or ())
    fixed_y = set(sy or ())
    def pinned(p):
        return p[0] in fixed_x or p[1] in fixed_y
    segs = []
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        segs.append((a, b, b[0] - a[0], b[1] - a[1]))
    def sign(v):
        return (v > 0) - (v < 0)
    def key(s):
        _, _, dx, dy = s
        return ('h', sign(dx), abs(dx)) if dy == 0 else ('v', sign(dy), abs(dy))
    # Find stair runs: alternating axes, constant step per axis, steps <= 2, monotonic.
    used = [False] * n
    runs = []
    for start in range(n):
        if used[start]:
            continue
        k0 = key(segs[start])
        if k0[2] > 2:
            continue
        run = [start]
        j = start
        while len(run) < n:
            nj = (j + 1) % n
            if used[nj]:
                break
            kj, kn = key(segs[j]), key(segs[nj])
            if kn[2] > 2 or kn[0] == kj[0]:
                break
            # same axis as two back must match exactly
            if len(run) >= 2 and key(segs[run[-2]]) != kn:
                break
            if pinned(segs[j][1]):
                break
            run.append(nj)
            j = nj
        if len(run) >= 3:
            # a stair needs both step lengths small and at least one of them 1
            ks = {key(segs[i])[2] for i in run}
            if 1 in ks:
                for i in run:
                    used[i] = True
                runs.append(run)
    if not runs:
        return pts
    run_of = {}
    for r in runs:
        for i in r:
            run_of[i] = r
    out = []
    # rotate to a segment not in a run if possible
    starts = [k for k in range(n) if k not in run_of]
    idx = starts[0] if starts else runs[0][0]
    visited = 0
    while visited < n:
        if idx in run_of and run_of[idx][0] == idx:
            r = run_of[idx]
            a0, b0 = segs[r[0]][0], segs[r[0]][1]
            a1, b1 = segs[r[-1]][0], segs[r[-1]][1]
            out.append(a0)
            out.append(Mid((a0[0] + b0[0]) / 2, (a0[1] + b0[1]) / 2))
            out.append(Mid((a1[0] + b1[0]) / 2, (a1[1] + b1[1]) / 2))
            visited += len(r)
            idx = (r[-1] + 1) % n
        else:
            out.append(segs[idx][0])
            visited += 1
            idx = (idx + 1) % n
    if extend:
        out = extend_runs(out, pinned)
    # drop duplicates and collinear points
    clean = []
    for p in out:
        if not clean or abs(clean[-1][0] - p[0]) > 1e-9 or abs(clean[-1][1] - p[1]) > 1e-9:
            clean.append(p)
    while len(clean) > 1 and clean[0] == clean[-1]:
        clean.pop()
    res = []
    m = len(clean)
    for i in range(m):
        a, b, c = clean[i - 1], clean[i], clean[(i + 1) % m]
        if abs((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])) > 1e-9 or pinned(b):
            res.append(b)
    return res


class Mid(tuple):
    """A stair-run midpoint (the ends of a straightened stair line)."""
    def __new__(cls, x, y):
        return super().__new__(cls, (x, y))


def extend_runs(out, pinned):
    """Moves each straightened stair line's ends onto the axis-aligned edges beside it, so the
    line meets them at a corner instead of a half-pixel jog."""
    n = len(out)
    res = [tuple(p) for p in out]
    for i in range(n):
        m0, m1 = out[i], out[(i + 1) % n]
        if not (isinstance(m0, Mid) and isinstance(m1, Mid)):
            continue
        dx, dy = m1[0] - m0[0], m1[1] - m0[1]
        if dx == 0 or dy == 0:
            continue
        # start: the corner before m0 and the axis-aligned edge leading into it
        a0, prev = out[i - 1], out[i - 2]
        x = cross_axis(prev, a0, m0, dx, dy)
        if x and not isinstance(a0, Mid) and not isinstance(prev, Mid) and not pinned(a0):
            res[i - 1] = x
            res[i] = x
        b1, nxt = out[(i + 2) % n], out[(i + 3) % n]
        x = cross_axis(nxt, b1, m1, dx, dy)
        if x and not isinstance(b1, Mid) and not isinstance(nxt, Mid) and not pinned(b1):
            res[(i + 2) % n] = x
            res[(i + 1) % n] = x
    return res


def cross_axis(far, near, m, dx, dy):
    """Where the line through m (direction dx, dy) crosses the axis-aligned edge far-near, if it
    crosses within the edge; None otherwise."""
    if far[0] == near[0]:  # vertical edge
        y = m[1] + (near[0] - m[0]) * dy / dx
        lo, hi = sorted((far[1], near[1]))
        return (near[0], y) if lo - 1e-9 <= y <= hi + 1e-9 else None
    if far[1] == near[1]:
        x = m[0] + (near[1] - m[1]) * dx / dy
        lo, hi = sorted((far[0], near[0]))
        return (x, near[1]) if lo - 1e-9 <= x <= hi + 1e-9 else None
    return None


def path_d(loops):
    return ' '.join('M' + ' L'.join(f'{f(x)} {f(y)}' for x, y in lp) + ' Z'
                    for lp in loops if len(lp) >= 3)


def trace(atlas, name, extend, tol=8, alpha_min=110, min_count=4):
    """Returns the SVG body paths, natural size and stretch bands for one atlas frame."""
    frames = json.loads((atlas / 'frames.json').read_text())['frames']
    box = frames[name + '.png']['frame']
    im = Image.open(atlas / 'atlas.png').convert('RGBA').crop(
        (box['x'], box['y'], box['x'] + box['w'], box['y'] + box['h']))
    w, h = im.size
    px = im.load()
    colors = palette(px, w, h, tol, alpha_min, min_count)
    label = [[-1] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if px[x, y][3] >= alpha_min:
                label[y][x] = nearest(px[x, y], colors)
    area = Counter(l for row in label for l in row if l >= 0)
    present = [i for i in range(len(colors)) if area[i]]
    translucent = [i for i in present if colors[i][3] < 245]
    opaque = sorted((i for i in present if colors[i][3] >= 245), key=lambda i: -area[i])
    layers = sorted(translucent, key=lambda i: -area[i]) + opaque
    sx, sy = bands(atlas, name)
    body = []
    for k, ci in enumerate(layers):
        later = set(layers[k:])
        if colors[ci][3] < 245:
            later = {ci} | {i for i in layers[k:] if colors[i][3] >= 245}
        mask = [[label[y][x] in later for x in range(w)] for y in range(h)]
        loops = [simplify(lp, sx, sy, extend) for lp in contours(mask, w, h)]
        c = colors[ci]
        attrs = f'fill="#{c[0]:02x}{c[1]:02x}{c[2]:02x}"'
        if c[3] < 245:
            attrs += f' fill-opacity="{f(c[3] / 255)}"'
        body.append(f'<path {attrs} fill-rule="evenodd" d="{path_d(loops)}"/>')
    return body, w, h, sx, sy


def svg_text(name, note, body, w, h, sx, sy):
    stretch = ''
    grid = []
    for axis, band in (('x', sx), ('y', sy)):
        if band:
            stretch += f' data-stretch-{axis}="{f(band[0])} {f(band[1])}"'
            grid.append(f'{axis} {f(band[0])}-{f(band[1])}')
    head = f'<!-- {name}: {note} Traced from the atlas frame.'
    head += f' Stretches along {" and ".join(grid)}.' if grid else ' Fixed size.'
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
             f'viewBox="0 0 {w} {h}"{stretch}>', f'  {head} -->']
    lines += [f'  {path}' for path in body]
    return '\n'.join(lines + ['</svg>']) + '\n'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--atlas', type=Path, required=True, help='extract_deca_ui.py output')
    parser.add_argument('--output', type=Path, default=ROOT / 'ui' / 'deca')
    parser.add_argument('--jog', action='store_true', help='keep half-pixel steps at stair ends')
    parser.add_argument('--note', default='Deca UI frame.', help='what the frame is used for')
    parser.add_argument('names', nargs='+')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in args.names:
        body, w, h, sx, sy = trace(args.atlas, name, not args.jog)
        out = args.output / f'{name}.svg'
        out.write_text(svg_text(name, args.note, body, w, h, sx, sy))
        print(f'wrote {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
