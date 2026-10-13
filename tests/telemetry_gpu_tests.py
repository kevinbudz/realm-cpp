#!/usr/bin/env python3
"""Actual GL asynchronous attribution/capture tests. All peers/data are local."""
import base64, json, math, pathlib, struct, subprocess, sys, tempfile, zlib
viewer=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='realm-telemetry-') as tmp:
    p=pathlib.Path(tmp)
    (p/'script').write_text(f'0 move 100 100\n0 capture {p}/script.png\n')
    # Offline world of one plain Grass tile per cell (no objects, so models_total stays 0 below).
    (p/'grass.jm').write_text(json.dumps({'width':20,'height':20,'dict':[{'ground':'Grass'}],'data':base64.b64encode(zlib.compress(struct.pack('>400h',*[0]*400))).decode()}))
    args=[str(viewer),'--hidden','--benchmark','--frames','64','--map',str(p/'grass.jm'),'--telemetry-warmup','4',
          '--telemetry',str(p/'frames.jsonl'),'--trace',str(p/'trace.json'),'--script',str(p/'script'),'--capture',str(p/'final.png')]
    subprocess.run(args,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    rows=[json.loads(x) for x in (p/'frames.jsonl').read_text().splitlines()]
    f={r['frame_id']:r for r in rows if r['type']=='frame'}
    g=[r for r in rows if r['type']=='gpu'];m={r['key']:r['value'] for r in rows if r['type']=='metadata'}
    assert len(f)==64 and set(f)==set(range(64))
    assert f[0]['capture'] and f[63]['capture'] and not f[1]['interval_valid']
    assert all(f[i]['warmup']==(i<4) for i in f)
    assert all(not f[i]['capture'] for i in range(1,63))
    assert f[0]['input_to_submit_ms'] is not None
    assert all(r['dropped_zones']==0 and r['drawable_width']==800 and r['drawable_height']==600 for r in f.values())
    assert len({r['frame_id'] for r in g})==len(g)>0
    assert len(g)+int(m['gpu_pending_at_exit'])+int(m['gpu_skipped_busy_slots'])==64
    assert int(m['gpu_pending_at_exit'])<=8
    for r in g:
        frame=f[r['frame_id']]
        assert r['normal']==(not frame['warmup'] and not frame['capture'])
        assert math.isclose(r['total_ms'],sum(r[x] for x in ('upload_ms','world_ms','effects_ms','ui_ms')),abs_tol=1e-6)
        assert all(r[x]>=0 and math.isfinite(r[x]) for x in ('total_ms','world_ms','models_ms'))
        assert r['models_measured']==r['models_total']==0
    trace=json.loads((p/'trace.json').read_text())['traceEvents']
    assert len(trace)==sum(r['type']=='zone' for r in rows)
    assert all(e['args']['frame_id'] in f and e['dur']>=0 for e in trace)
    # More model draws than the fixed timer budget must retain all draws while
    # explicitly reporting only the measured subset. Consecutive copies of one model
    # are one instanced draw (painter order is (depth, id) and map ids follow object
    # type), so each of the 8 columns uses a different registered model: 40 draws.
    kinds=['Cube','Big Cube','Ico','Octa','Pyramid','Tetra','Dodec','Table']
    grid=[0]*(20*20)
    for y in range(7,12):
        for x in range(6,14):grid[y*20+x]=1+(x-6)
    grid[10*20+10]=1+len(kinds) # Explicit spawn (on the Pyramid column) prevents fallback to the first walkable corner.
    data=base64.b64encode(zlib.compress(struct.pack('>'+str(len(grid))+'h',*grid))).decode()
    entries=[{'ground':'Grass'}]+[{'ground':'Grass','objs':[{'id':k}]} for k in kinds]+[{'ground':'Grass','objs':[{'id':'Pyramid'}],'regions':[{'id':'Spawn'}]}]
    (p/'models.jm').write_text(json.dumps({'width':20,'height':20,'dict':entries,'data':data}))
    subprocess.run([str(viewer),'--hidden','--benchmark','--frames','64','--map',str(p/'models.jm'),'--telemetry-warmup','4',
                    '--telemetry',str(p/'models.jsonl')],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    model_rows=[json.loads(x) for x in (p/'models.jsonl').read_text().splitlines()]
    model_frames=[r for r in model_rows if r['type']=='frame']
    model_gpu=[r for r in model_rows if r['type']=='gpu']
    assert model_gpu and all(r['models_total']==40 and r['models_measured']==32 for r in model_gpu)
    # Model geometry is uploaded once with the renderer; frames upload only instance quads.
    assert all(r['counters']['models']==40 and r['counters']['mesh_upload_bytes']==0 and r['counters']['draw_calls']>=40 for r in model_frames)
    assert all(r['models_ms']<=r['total_ms'] for r in model_gpu)
    bad=subprocess.run([str(viewer),'--hidden','--frames','1','--telemetry',str(p/'same'),'--trace',str(p/'same')],capture_output=True)
    assert bad.returncode!=0
print('GPU telemetry: frame IDs, phase units, bounded pending ring, uploads/draws, capture and input attribution pass')
