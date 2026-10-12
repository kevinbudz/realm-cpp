#!/usr/bin/env python3
"""Summarize realm-frame-v1 JSONL; exclude warmup/capture and tainted intervals.
CPU zones are inclusive; models_ms is a subset of GPU world/UI, never added.
"""
import argparse, collections, json, math, pathlib, statistics

def distribution(values):
    values=sorted(values)
    if not values:return {'samples':0}
    return {'samples':len(values),'median':statistics.median(values),'p95':values[math.ceil(.95*len(values))-1],
            'p99':values[math.ceil(.99*len(values))-1],'max':values[-1]}

def report(path):
    metadata={};frames={};zones=collections.defaultdict(list);gpu=[]
    with open(path) as file:
        for line in file:
            r=json.loads(line)
            if r['type']=='metadata':metadata[r['key']]=r['value']
            elif r['type']=='frame':
                if r['frame_id'] in frames:raise ValueError('Duplicate frame ID')
                frames[r['frame_id']]=r
            elif r['type']=='zone':zones[r['frame_id']].append(r)
            elif r['type']=='gpu':gpu.append(r)
    selected={i:f for i,f in frames.items() if not f['warmup'] and not f['capture']}
    metrics={k:distribution([f[k] for f in selected.values() if f[k] is not None and (k!='interval_ms' or f['interval_valid'])])
             for k in ('interval_ms','cpu_ms','present_ms','paced_ms','input_to_submit_ms')}
    per_zone=collections.defaultdict(lambda: {'durations_us':[], 'per_frame_ms':[], 'allocations':0,'allocation_bytes':0})
    for i in selected:
        totals=collections.defaultdict(float)
        for z in zones[i]:
            s=per_zone[z['name']];s['durations_us'].append(z['duration_us']);s['allocations']+=z['allocations'];s['allocation_bytes']+=z['allocation_bytes'];totals[z['name']]+=z['duration_us']/1000
        for name,ms in totals.items():per_zone[name]['per_frame_ms'].append(ms)
    zone_metrics={name:{'invocations':len(s['durations_us']),'per_invocation_us':distribution(s['durations_us']),
                       'per_frame_ms':distribution(s['per_frame_ms']+[0]*(len(selected)-len(s['per_frame_ms']))),
                       'allocations_inclusive':s['allocations'],'allocation_bytes_inclusive':s['allocation_bytes']}
                  for name,s in per_zone.items()}
    gpu_ids=set();gselected=[]
    for g in gpu:
        if g['frame_id'] not in frames or g['frame_id'] in gpu_ids:raise ValueError('Invalid GPU frame attribution')
        gpu_ids.add(g['frame_id']);f=frames[g['frame_id']]
        if bool(g['normal']) != (not f['warmup'] and not f['capture']):raise ValueError('Invalid GPU normal/capture attribution')
        if g['normal']:gselected.append(g)
    gpu_metrics={k:distribution([g[k] for g in gselected]) for k in ('total_ms','upload_ms','world_ms','effects_ms','ui_ms','models_ms')}
    counters={k:{'sum':sum(f['counters'][k] for f in selected.values()),'max':max((f['counters'][k] for f in selected.values()),default=0),
                 'last':list(frames.values())[-1]['counters'][k]} for k in next(iter(frames.values()))['counters']} if frames else {}
    return {'source':str(path),'metadata':metadata,'frames':len(frames),'normal_frames':len(selected),
            'capture_frames':[i for i,f in frames.items() if f['capture']],
            'dropped_zones':sum(f['dropped_zones'] for f in frames.values()),'cpu_ms':metrics,'gpu_ms':gpu_metrics,
            'gpu_frames':len(gpu),'gpu_missing_frames':sorted(set(frames)-gpu_ids),
            'partial_model_frames':sum(g['models_measured']<g['models_total'] for g in gselected),
            'zones':zone_metrics,'counters':counters}

def markdown(r):
    lines=['# Telemetry report','',f"{r['normal_frames']} normal frames / {r['frames']} exported; capture frames {r['capture_frames']}. Dropped zones: {r['dropped_zones']}.",'',
           'Durations are milliseconds. Warmup and capture frames are excluded; the interval after capture is excluded. CPU/GPU overlap.','',
           '| Metric | Samples | Median | p95 | p99 | Max |','| --- | ---: | ---: | ---: | ---: | ---: |']
    for group in ('cpu_ms','gpu_ms'):
        for name,s in r[group].items():lines.append('| '+group+'/'+name+' | '+str(s['samples'])+' | '+' | '.join(f"{s[k]:.6f}" if k in s else '—' for k in ('median','p95','p99','max'))+' |')
    lines+=['','CPU zones below are inclusive. Their times and allocation totals must not be added across nested zones. Allocation bytes are traffic, not resident memory.','',
            '| Zone | Calls | p95 ms/frame | Inclusive allocations | Inclusive bytes |','| --- | ---: | ---: | ---: | ---: |']
    for name,s in sorted(r['zones'].items(),key=lambda x:x[1]['per_frame_ms'].get('p95',0),reverse=True):
        lines.append(f"| {name} | {s['invocations']} | {s['per_frame_ms'].get('p95',0):.6f} | {s['allocations_inclusive']} | {s['allocation_bytes_inclusive']} |")
    lines+=['','| Counter/gauge | Sum (counters only) | Max | Last exported |','| --- | ---: | ---: | ---: | ---: |']
    for name,s in r['counters'].items():lines.append(f"| {name} | {s['sum']} | {s['max']} | {s['last']} |")
    lines+=['',f"GPU frames resolved: {r['gpu_frames']}; unresolved: {r['gpu_missing_frames']}. Partial model samples: {r['partial_model_frames']}.",'']
    return '\n'.join(lines)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('samples',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args();r=report(args.samples);args.output.with_suffix('.json').write_text(json.dumps(r,indent=2)+'\n');args.output.with_suffix('.md').write_text(markdown(r))
    print(f"Exported {args.output.with_suffix('.md')}: {r['normal_frames']} normal frames; {r['gpu_frames']} resolved GPU frames")
