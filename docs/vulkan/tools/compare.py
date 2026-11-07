"""Compare two RGBA captures: size, differing-pixel %, max channel delta, delta histogram, bbox, diff image.
Usage: compare.py A.png B.png [DIFF.png]"""
import sys
import numpy as np
from PIL import Image

a = np.asarray(Image.open(sys.argv[1]).convert("RGBA")).astype(np.int16)
b = np.asarray(Image.open(sys.argv[2]).convert("RGBA")).astype(np.int16)
if a.shape != b.shape:
    print(f"SIZE MISMATCH {a.shape} vs {b.shape}")
    sys.exit(1)
delta = np.abs(a - b)                      # per channel
per_pixel = delta.max(axis=2)              # worst channel per pixel
total = per_pixel.size
differing = int((per_pixel > 0).sum())
within1 = int((per_pixel <= 1).sum())
print(f"size {a.shape[1]}x{a.shape[0]}  pixels {total}")
print(f"differing pixels {differing} ({100.0 * differing / total:.4f}%)")
print(f"max channel delta {int(delta.max())}")
print(f"pixels with delta <= 1: {100.0 * within1 / total:.4f}%")
hist = np.bincount(per_pixel.ravel(), minlength=256)
top = [(d, int(hist[d])) for d in range(1, 256) if hist[d]][:8]
print(f"delta histogram (delta, pixels): {top}")
ys, xs = np.nonzero(per_pixel)
if ys.size:
    print(f"diff bbox x[{xs.min()},{xs.max()}] y[{ys.min()},{ys.max()}]")
if len(sys.argv) > 3:
    out = np.zeros_like(a, dtype=np.uint8)
    out[..., 3] = 255
    out[..., 0] = np.clip(per_pixel * 32, 0, 255).astype(np.uint8)
    out[..., 1] = 0
    out[..., 2] = np.where(per_pixel > 0, 0, 255).astype(np.uint8)
    Image.fromarray(out).save(sys.argv[3])
    print(f"diff image {sys.argv[3]}")
