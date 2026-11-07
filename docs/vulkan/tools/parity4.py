"""Four labelled pairs per fixture from evidence/s8-parity (or a dir given as argv[1]).

Pairs: GL-llvmpipe vs GL-RADV, VK-lavapipe vs VK-RADV, GL-llvmpipe vs VK-lavapipe, GL-RADV vs VK-RADV.
Per pair: identical %, delta<=1 %, max channel delta, differing px count, number of 16x16 tiles
where more than 25% of pixels differ (a region-wide indicator), and the diff bbox.
"""
import os
import sys
import numpy as np
from PIL import Image

DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
PAIRS = [
    ("GL-lvp vs GL-RADV", "gl-lvp", "gl-radv"),
    ("VK-lvp vs VK-RADV", "vk-lvp", "vk-radv"),
    ("GL-lvp vs VK-lvp", "gl-lvp", "vk-lvp"),
    ("GL-RADV vs VK-RADV", "gl-radv", "vk-radv"),
]
FIXTURES = ["S1-enemies-dense", "S3-enemies-dense", "S3-players-dense", "S3-enemies-spread",
            "S2-enemies-dense", "S1-players-dense", "S3-combat-cluster", "S1-projectiles-dense",
            "S3-projectiles-dense", "S3-models-dense", "nexus-pan"]


def load(path):
    return np.asarray(Image.open(path).convert("RGBA")).astype(np.int16)


def stats(a, b):
    delta = np.abs(a - b).max(axis=2)
    total = delta.size
    diff = delta > 0
    h, w = delta.shape
    tiles = 0
    tile_hits = 0
    for ty in range(0, h, 16):
        for tx in range(0, w, 16):
            block = diff[ty:ty + 16, tx:tx + 16]
            if block.size:
                tiles += 1
                if block.mean() > 0.25:
                    tile_hits += 1
    ys, xs = np.nonzero(diff)
    bbox = f"x[{xs.min()},{xs.max()}] y[{ys.min()},{ys.max()}]" if ys.size else "-"
    return {
        "identical": 100.0 * (1 - diff.sum() / total),
        "le1": 100.0 * (delta <= 1).sum() / total,
        "max": int(delta.max()),
        "diff_px": int(diff.sum()),
        "region_tiles": tile_hits,
        "tiles": tiles,
        "bbox": bbox,
    }


def main():
    print(f"{'fixture':20} {'pair':20} {'identical%':>10} {'<=1%':>8} {'max':>4} {'diffpx':>7} "
          f"{'region16':>9} bbox")
    for fx in FIXTURES:
        for label, left, right in PAIRS:
            pa = os.path.join(DIR, f"{left}-{fx}.png")
            pb = os.path.join(DIR, f"{right}-{fx}.png")
            if not (os.path.exists(pa) and os.path.exists(pb)):
                print(f"{fx:20} {label:20} missing")
                continue
            a, b = load(pa), load(pb)
            if a.shape != b.shape:
                print(f"{fx:20} {label:20} SIZE MISMATCH")
                continue
            s = stats(a, b)
            print(f"{fx:20} {label:20} {s['identical']:10.4f} {s['le1']:8.4f} {s['max']:4d} "
                  f"{s['diff_px']:7d} {s['region_tiles']:4d}/{s['tiles']:<4d} {s['bbox']}")


if __name__ == "__main__":
    main()
