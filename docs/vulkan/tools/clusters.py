"""Connected clusters (8-neighbourhood) of differing pixels between two captures. Usage: clusters.py A B"""
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert("RGBA")).astype(int)
b = np.asarray(Image.open(sys.argv[2]).convert("RGBA")).astype(int)
mask = np.abs(a - b).max(axis=2) > 0
h, w = mask.shape
seen = np.zeros_like(mask)
sizes = []
boxes = []
for y0, x0 in zip(*np.nonzero(mask)):
    if seen[y0, x0]:
        continue
    stack = [(y0, x0)]
    seen[y0, x0] = True
    pts = []
    while stack:
        y, x = stack.pop()
        pts.append((y, x))
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
    ys = [p[0] for p in pts]; xs = [p[1] for p in pts]
    sizes.append(len(pts))
    boxes.append((min(xs), min(ys), max(xs), max(ys)))
sizes = np.array(sizes)
print(f"clusters {sizes.size}; sizes: max {sizes.max() if sizes.size else 0}, "
      f"<=2 px: {int((sizes<=2).sum())}, <=9 px: {int((sizes<=9).sum())}, >9 px: {int((sizes>9).sum())}")
for i in np.argsort(-sizes)[:3]:
    print("  largest cluster", int(sizes[i]), "bbox x%d..%d y%d..%d" % (boxes[i][0], boxes[i][2], boxes[i][1], boxes[i][3]))
