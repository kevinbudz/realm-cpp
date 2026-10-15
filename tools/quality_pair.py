#!/usr/bin/env python3
"""Paired headless before/after run of realm_stress fixtures (World + Combat + Session replay, no renderer).

Runs the same checked-in manifest through two realm_stress binaries in alternating order, requires identical
simulation outcomes (state/packet checksums, hits, spawned/removed counts), and reports median per-tick zone times.
Component evidence only: it excludes Scene, text, textures and the GPU.

  python3 tools/quality_pair.py --before build/quality/stress-before --after build/tests/realm_stress \
      --scenarios S3-mixed-dense,S3-mixed-spread --ticks 60 --repeats 5 --output build/quality/pair
"""
import argparse, collections, json, pathlib, statistics, subprocess, sys

IGNORED = {"rss_mb_first", "rss_mb_peak", "rss_mb_last", "cache_peak", "cache_last"}
ZONES = ["fixture.timeline", "combat.update", "combat.target_query", "world.update", "network.decode_dispatch", "fixture.framed_replay"]


def run(binary, manifest, ticks, out):
    out.parent.mkdir(parents=True, exist_ok=True)
    summary, trace = out.with_suffix(".json"), out.with_suffix(".jsonl")
    subprocess.run([binary, str(manifest), str(ticks), str(summary), "0", str(trace)], check=True, stdout=subprocess.DEVNULL)
    zones = collections.defaultdict(lambda: collections.defaultdict(float))
    for line in trace.open():
        row = json.loads(line)
        if row["type"] == "zone":
            zones[row["name"]][row["frame_id"]] += row["duration_us"] / 1000
    return json.loads(summary.read_text()), {n: statistics.median(v.values()) for n, v in zones.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", required=True)
    ap.add_argument("--after", required=True)
    ap.add_argument("--scenarios", required=True)
    ap.add_argument("--ticks", type=int, default=60)
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--fixtures", default="tests/fixtures")
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    out = pathlib.Path(args.output)
    report, failed = {}, False
    for scenario in args.scenarios.split(","):
        manifest = pathlib.Path(args.fixtures) / f"{scenario}.json"
        samples = {"before": [], "after": []}
        outcomes = {}
        for repeat in range(args.repeats):
            order = ("before", "after") if repeat % 2 == 0 else ("after", "before")
            for variant in order:
                binary = args.before if variant == "before" else args.after
                summary, zones = run(binary, manifest, args.ticks, out / f"{scenario}-{variant}-{repeat}")
                samples[variant].append(zones)
                outcomes.setdefault(variant, []).append({k: v for k, v in summary.items() if k not in IGNORED})
        same = all(o == outcomes["before"][0] for o in outcomes["before"] + outcomes["after"])
        failed |= not same
        row = {"identical_outcomes": same, "zones": {}}
        for zone in ZONES:
            b = [s[zone] for s in samples["before"] if zone in s]
            a = [s[zone] for s in samples["after"] if zone in s]
            if b and a:
                row["zones"][zone] = {"before_ms": statistics.median(b), "after_ms": statistics.median(a),
                                      "before_range": [min(b), max(b)], "after_range": [min(a), max(a)],
                                      "ratio": statistics.median(a) / statistics.median(b) if statistics.median(b) else None}
        summary = outcomes["after"][0]
        row["rss_mb_peak"] = {v: [json.loads((out / f"{scenario}-{v}-{r}.json").read_text()).get("rss_mb_peak") for r in range(args.repeats)] for v in samples}
        report[scenario] = row
        line = ", ".join(f"{z.split('.')[-1]} {d['before_ms']:.2f}->{d['after_ms']:.2f}" for z, d in row["zones"].items() if z in ("fixture.timeline", "combat.update", "world.update"))
        print(f"{scenario:22s} outcomes {'identical' if same else 'DIFFER'}  {line}")
    (out / "report.json").write_text(json.dumps(report, indent=2))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
