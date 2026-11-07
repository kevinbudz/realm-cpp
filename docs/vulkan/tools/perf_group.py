"""Pool measured frames per configuration (name minus -rN) over reps, then print one row each.

Usage: perf_group.py DIR   (reads DIR/*-rN.jsonl)
"""
import glob
import json
import os
import re
import statistics
import sys

def load(path):
    frames, gpu = [], []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            rec = json.loads(line)
            if rec.get("type") == "frame" and rec.get("warmup", 0) == 0:
                frames.append(rec)
            elif rec.get("type") == "gpu" and rec.get("normal", 0) == 1:
                gpu.append(rec)
    return frames, gpu

def pct(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, int(round(q * (len(values) - 1))))]

def main(directory):
    groups = {}
    for path in sorted(glob.glob(os.path.join(directory, "*-r[0-9].jsonl"))):
        name = re.sub(r"-r\d\.jsonl$", "", os.path.basename(path))
        groups.setdefault(name, []).append(path)
    print(f"{'config':16} {'reps':>4} {'n':>5} {'mean_ms':>8} {'med_ms':>7} {'p95_ms':>7} {'fps_mean':>9} "
          f"{'cpu_med':>8} {'cpu_mean':>8} {'gpu_med':>8} {'gpu_mean':>8} {'gpu_p95':>8}")
    for name in sorted(groups):
        intervals, cpu, gpu = [], [], []
        for path in groups[name]:
            frames, gpus = load(path)
            intervals += [f["interval_ms"] for f in frames if f.get("interval_valid") == 1]
            cpu += [f["cpu_ms"] for f in frames]
            gpu += [g["total_ms"] for g in gpus]
        mean = statistics.fmean(intervals)
        print(f"{name:16} {len(groups[name]):>4} {len(intervals):>5} {mean:8.3f} "
              f"{statistics.median(intervals):7.3f} {pct(intervals, 0.95):7.3f} {1000 / mean:9.1f} "
              f"{statistics.median(cpu):8.3f} {statistics.fmean(cpu):8.3f} "
              f"{statistics.median(gpu) if gpu else float('nan'):8.3f} "
              f"{statistics.fmean(gpu) if gpu else float('nan'):8.3f} "
              f"{pct(gpu, 0.95) if gpu else float('nan'):8.3f}")

if __name__ == "__main__":
    main(sys.argv[1])
