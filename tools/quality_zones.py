#!/usr/bin/env python3
"""Median zone/counter/GPU summary of quality_stress.py frame logs.

Usage: quality_zones.py DIR[:VARIANT] [...] [--scenarios a,b] [--zones z1,z2] [--counters c1,c2]
VARIANT selects one side of a --compare run (current or compare); default all logs.
Per scenario (all repeats pooled, measured frames only): frame CPU p50/p95, the
median per-frame duration (ms) of each requested zone, the median of each requested
counter and the GPU total/world medians. Several directories print side by side.
"""
import argparse, collections, json, statistics
from pathlib import Path

ZONES = 'scene.build,model.prepare,render.submit,render.quad_upload,render.model_upload_submit'
COUNTERS = 'models,draw_calls,mesh_upload_bytes,triangles,allocations'


def pct(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, int(q * len(values)))] if values else float('nan')


def summarize(source, scenario, zones, counters):
    directory, _, variant = str(source).partition(':')
    frames, zone_ms, gpu_total, gpu_world = [], collections.defaultdict(dict), [], []
    counter_values = collections.defaultdict(list)
    for log in sorted(Path(directory).glob(f'{scenario}-[0-9]*-{variant or "*"}.jsonl')):
        measured = set()
        for line in log.open():
            j = json.loads(line)
            kind = j.get('type')
            key = (log.name, j.get('frame_id'))
            if kind == 'frame' and not j['warmup']:
                measured.add(key); frames.append(j['cpu_ms'])
                for c in counters: counter_values[c].append(j['counters'].get(c, 0))
            elif kind == 'zone' and j['name'] in zones:
                zone_ms[j['name']][key] = zone_ms[j['name']].get(key, 0) + j['duration_us'] / 1000
            elif kind == 'gpu' and j.get('normal') and key in measured:
                gpu_total.append(j['total_ms']); gpu_world.append(j['world_ms'])
        for name in zones:
            for k in list(zone_ms[name]):
                if k[0] == log.name and k not in measured: del zone_ms[name][k]
    if not frames: return None
    result = {'frames': len(frames), 'cpu_p50': pct(frames, .5), 'cpu_p95': pct(frames, .95)}
    for name in zones:
        values = list(zone_ms[name].values()) + [0.0] * (len(frames) - len(zone_ms[name]))
        result[name] = statistics.median(values)
    for c in counters: result[c] = statistics.median(counter_values[c])
    result['gpu_total'] = statistics.median(gpu_total) if gpu_total else float('nan')
    result['gpu_world'] = statistics.median(gpu_world) if gpu_world else float('nan')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dirs', nargs='+')
    p.add_argument('--scenarios')
    p.add_argument('--zones', default=ZONES)
    p.add_argument('--counters', default=COUNTERS)
    p.add_argument('--json', type=Path)
    a = p.parse_args()
    zones, counters = a.zones.split(','), a.counters.split(',')
    scenarios = a.scenarios.split(',') if a.scenarios else sorted({f.name.rsplit('-', 2)[0] for d in a.dirs for f in Path(d.partition(':')[0]).glob('*-[0-9]*-*.jsonl')})
    out = {}
    for s in scenarios:
        print(f'## {s}')
        rows = {str(d): summarize(d, s, zones, counters) for d in a.dirs}
        out[s] = rows
        keys = next((list(r.keys()) for r in rows.values() if r), [])
        for k in keys:
            print(f'  {k:28}' + ''.join(f'{(r or {}).get(k, float("nan")):>16.4g}' for r in rows.values()))
    if a.json: a.json.write_text(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
