"""Summarise realm telemetry JSONL files: frame interval, cpu_ms and gpu total_ms.

Usage: perf_stats.py FILE.jsonl [FILE.jsonl ...]
Measured frames only (warmup == 0). Interval uses interval_valid == 1 frames.
Prints one line per file: n, interval mean/median/p95 (ms), FPS from the mean,
cpu_ms median/mean, gpu total_ms median/mean/p95 (normal GPU records).
"""
import json
import statistics
import sys


def pct(values, q):
    values = sorted(values)
    if not values:
        return float("nan")
    idx = min(len(values) - 1, max(0, int(round(q * (len(values) - 1)))))
    return values[idx]


def summarise(path):
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
    intervals = [f["interval_ms"] for f in frames if f.get("interval_valid") == 1]
    cpu = [f["cpu_ms"] for f in frames]
    gpu_total = [g["total_ms"] for g in gpu]
    mean_i = statistics.fmean(intervals) if intervals else float("nan")
    med_i = statistics.median(intervals) if intervals else float("nan")
    fps = 1000.0 / mean_i if intervals else float("nan")
    gpu_med = statistics.median(gpu_total) if gpu_total else float("nan")
    gpu_mean = statistics.fmean(gpu_total) if gpu_total else float("nan")
    return {
        "file": path,
        "n": len(frames),
        "n_interval": len(intervals),
        "interval_mean": mean_i,
        "interval_median": med_i,
        "interval_p95": pct(intervals, 0.95),
        "fps_mean": fps,
        "cpu_median": statistics.median(cpu) if cpu else float("nan"),
        "cpu_mean": statistics.fmean(cpu) if cpu else float("nan"),
        "gpu_n": len(gpu_total),
        "gpu_median": gpu_med,
        "gpu_mean": gpu_mean,
        "gpu_p95": pct(gpu_total, 0.95),
    }


def main(paths):
    for path in paths:
        s = summarise(path)
        print(
            f"{s['file']}: n={s['n']} int_mean={s['interval_mean']:.3f} "
            f"int_med={s['interval_median']:.3f} int_p95={s['interval_p95']:.3f} "
            f"fps={s['fps_mean']:.1f} cpu_med={s['cpu_median']:.3f} cpu_mean={s['cpu_mean']:.3f} "
            f"gpu_n={s['gpu_n']} gpu_med={s['gpu_median']:.3f} gpu_mean={s['gpu_mean']:.3f} "
            f"gpu_p95={s['gpu_p95']:.3f}"
        )


if __name__ == "__main__":
    main(sys.argv[1:])
