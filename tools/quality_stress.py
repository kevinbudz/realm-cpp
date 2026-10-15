#!/usr/bin/env python3
"""Versioned native client stress runner. Durations are simulation seconds.
All runs do identical ticks; report wall time separately. No user credentials.
Full defaults: 10s warmup + 60s measured, five repetitions. Use short explicit
settings for cost discovery at overload populations, never for qualification.
"""
import argparse,hashlib,json,math,os,platform,random,statistics,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIXTURES=ROOT/'tests/fixtures'
LEVELS={'S1':(250,250,250,2000),'S2':(1000,1000,1000,10000),'S3':(3000,3000,3000,20000),'S4':(6000,6000,6000,40000)}
def write(p,j):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(j,indent=2)+'\n')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def generate(driver):
    subprocess.run([str(driver),'--describe',str(FIXTURES/'catalog.json')],check=True)
    base=json.loads((FIXTURES/'catalog.json').read_text());scenarios={}
    def add(name,**kw):
        c=base|dict(players=0,enemies=0,models=0,projectiles=0,arrangement='dense',moving=False,camera='idle',working_set=16)|kw
        write(FIXTURES/(name+'.json'),c);scenarios[name]=name+'.json'
    for level,pop in LEVELS.items():
        for arrangement in ['dense','spread']:
            for axis,n in zip(['players','enemies','models','projectiles'],pop):
                add(f'{level}-{axis}-{arrangement}',workload='COMBAT' if axis=='projectiles' else 'MODELS' if axis=='models' else 'POPULATION',**{axis:n},arrangement=arrangement)
            add(f'{level}-mixed-{arrangement}',workload='MIXED',players=pop[0],enemies=pop[1],models=pop[2],projectiles=pop[3],arrangement=arrangement,moving=True,working_set=512)
        add(f'{level}-mixed-cluster',workload='MIXED',players=pop[0],enemies=pop[1],models=pop[2],projectiles=pop[3],arrangement='cluster',moving=True,working_set=512)
        add(f'{level}-combat-cluster',workload='COMBAT',players=pop[0],enemies=pop[1],projectiles=pop[3],arrangement='cluster',moving=True)
    for camera in ['idle','pan','rotate','zoom']:
        add('nexus-'+camera,workload='NEXUS',camera=camera,map='../../../realm-server/Resources/Worlds/Nexus/0.jm')
    add('models-varied-rotating',workload='MODELS',models=1000,rotate_models=True,camera='rotate')
    add('models-repeated',workload='MODELS',models=1000,model_objects=['Cube'])
    for size in [16,512]:add('variety-'+str(size),workload='VARIETY',players=600,working_set=size,moving=True)
    add('churn',workload='CHURN',players=250,enemies=250,models=250,projectiles=2000,reset_every=100,replace_every=5,moving=True,working_set=512)
    add('ui',workload='UI',players=16,enemies=16,moving=True)
    add('determinism',workload='MIXED',players=8,enemies=8,models=0,projectiles=48,moving=True,working_set=16,reset_every=80,replace_every=10)
    write(FIXTURES/'index.json',dict(version=1,levels=LEVELS,scenarios=scenarios,clock='20ms, NEWTICK every 100ms with production 127ms interpolation',counts_order=['players','enemies','models','projectiles','other']))
def percentile(x,p):
    x=sorted(x);return x[min(len(x)-1,math.ceil(len(x)*p)-1)] if x else None
def stats(x):return dict(median=statistics.median(x),p95=percentile(x,.95),p99=percentile(x,.99),max=max(x)) if x else None
def report(path):
    frames=[];zones={};gpu=[];metadata={}
    for line in path.read_text().splitlines():
        r=json.loads(line)
        if r['type']=='frame' and not r['warmup'] and not r['capture']:frames.append(r)
        elif r['type']=='zone':zones.setdefault(r['frame_id'],[]).append(r)
        elif r['type']=='gpu' and r['normal']:gpu.append(r)
        elif r['type']=='metadata':metadata[r['key']]=r['value']
    assert frames,'No measured frames'
    zone_samples={}
    for f in frames:
        per={}
        for z in zones.get(f['frame_id'],[]):per[z['name']]=per.get(z['name'],0)+z['duration_us']/1000
        for name,value in per.items():zone_samples.setdefault(name,[]).append(value)
    return dict(cpu_ms=stats([r['cpu_ms'] for r in frames]),frame_interval_ms=stats([r['interval_ms'] for r in frames if r['interval_valid']]),present_ms=stats([r['present_ms'] for r in frames]),gpu_ms=stats([r['total_ms'] for r in gpu]),zones_ms={n:stats(v) for n,v in zone_samples.items()},counters={n:stats([f['counters'][n] for f in frames]) for n in frames[0]['counters']},drawable=[frames[-1]['drawable_width'],frames[-1]['drawable_height']],metadata=metadata,measured_frames=len(frames),gpu_frames=len(gpu))
def summarize(out,runs,ticks,warmup,repeats,compare=False,plot=False):
    lines=['# Native stress results','',f'Fixed {ticks} simulation ticks, warmup {warmup}, repetitions {repeats}. Durations are simulation time; hidden renders are component/mixed client evidence, not display latency.','', '| Scenario | Variant | CPU p95 median (ms) | Repeat range | Last resident / submitted |', '| --- | --- | ---: | --- | --- |']
    names=list(dict.fromkeys(r['scenario'] for r in runs))
    for name in names:
        for variant in sorted(set(r['variant'] for r in runs)):
            rr=[r for r in runs if r['scenario']==name and r['variant']==variant];values=[r['metrics']['cpu_ms']['p95'] for r in rr];last=rr[-1]['summary'];lines.append(f"| {name} | {variant} | {statistics.median(values):.3f} | {min(values):.3f}–{max(values):.3f} | {last['resident']} / {last['submitted_last']} |")
        if compare:
            med={v:statistics.median([r['metrics']['cpu_ms']['p95'] for r in runs if r['scenario']==name and r['variant']==v]) for v in ['current','compare']};lines.append(f"\n{name}: paired p95 ratio current/compare {med['current']/med['compare']:.4f}; >1.05 requires investigation.\n")
    (out/'report.md').write_text('\n'.join(lines)+'\n')
    if plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
        for axis in ['players','enemies','models','projectiles']:
            points=[]
            for level,pop in LEVELS.items():
                values=[r['metrics']['cpu_ms']['p95'] for r in runs if r['scenario']==f'{level}-{axis}-dense' and r['variant']=='current']
                if values:points.append((pop[['players','enemies','models','projectiles'].index(axis)],statistics.median(values),min(values),max(values)))
            if points:
                x,y,lo,hi=zip(*points);axes[0].errorbar(x,y,yerr=[[v-l for v,l in zip(y,lo)],[h-v for v,h in zip(y,hi)]],marker='o',capsize=3,label=axis)
        for arrangement in ['dense','spread','cluster']:
            points=[]
            for level,pop in LEVELS.items():
                values=[r['metrics']['cpu_ms']['p95'] for r in runs if r['scenario']==f'{level}-mixed-{arrangement}' and r['variant']=='current']
                if values:points.append((sum(pop),statistics.median(values),min(values),max(values)))
            if points:
                x,y,lo,hi=zip(*points);axes[1].errorbar(x,y,yerr=[[v-l for v,l in zip(y,lo)],[h-v for v,h in zip(y,hi)]],marker='o',capsize=3,label=arrangement)
        for ax in axes:
            ax.set_xscale('log');ax.set_yscale('log');ax.set_ylabel('CPU p95 (ms), repeat median / range');ax.grid(True,alpha=.25);ax.axhline(1000/144,color='#a33',ls='--',label='144 Hz budget (unqualified)');ax.legend(fontsize=8)
        axes[0].set_title('Dense individual axes');axes[0].set_xlabel('Requested remote actors or active projectiles')
        axes[1].set_title('Combined populations, actual survivors differ');axes[1].set_xlabel('Requested remote actors + projectiles')
        fig.suptitle('Short scaling discovery — full quality, native 1920×1080\nNot sustained capacity; overlapping CPU zones are not added',fontsize=11)
        fig.savefig(out/'scaling-curves.png',dpi=160);plt.close(fig)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--viewer',type=Path,default=ROOT/'build/realm_client');p.add_argument('--driver',type=Path,default=ROOT/'build/tests/realm_stress');p.add_argument('--generate',action='store_true');p.add_argument('--list',action='store_true');p.add_argument('--scenarios',default='nexus-idle,ui,churn,S1-mixed-dense');p.add_argument('--output',type=Path,default=ROOT/'build/quality-stress');p.add_argument('--warmup-seconds',type=float,default=10);p.add_argument('--measure-seconds',type=float,default=60);p.add_argument('--repeats',type=int,default=5);p.add_argument('--width',type=int,default=1920);p.add_argument('--height',type=int,default=1080);p.add_argument('--visible',action='store_true');p.add_argument('--capture',action='store_true');p.add_argument('--compare',type=Path);p.add_argument('--report-only',action='store_true');p.add_argument('--plot',action='store_true');p.add_argument('--trace',action='store_true');p.add_argument('--with-peers',action='store_true');p.add_argument('--timeout',type=int,default=7200)
    a=p.parse_args()
    if a.report_only:
        out=a.output.resolve();runs=json.loads((out/'results.json').read_text())['runs'];env=json.loads((out/'environment.json').read_text())
        for r in runs:r['metrics']=report(Path(r['raw']))
        write(out/'results.json',dict(version=1,runs=runs));summarize(out,runs,env['warmup_ticks']+env['measure_ticks'],env['warmup_ticks'],env['repeats'],any(r['variant']=='compare' for r in runs),a.plot);return
    if a.generate:generate(a.driver.resolve())
    index=json.loads((FIXTURES/'index.json').read_text());names=list(index['scenarios']) if a.scenarios=='all' else a.scenarios.split(',')
    if a.list:print('\n'.join(index['scenarios']));return
    if a.warmup_seconds<0 or a.measure_seconds<=0 or a.repeats<1:p.error('Invalid duration/repetitions')
    warmup=round(a.warmup_seconds*50);ticks=warmup+max(1,round(a.measure_seconds*50));out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=[*(p for p in ROOT.glob('src/**/*') if p.is_file()),*(p for p in ROOT.glob('include/realm/**/*') if p.is_file()),ROOT/'CMakeLists.txt']
    inventory={str(f.relative_to(ROOT)):digest(f) for f in source if f.is_file()}
    manifests={n:digest(FIXTURES/index['scenarios'][n]) for n in names}
    write(out/'environment.json',dict(platform=platform.platform(),cpu=platform.processor(),uname=list(platform.uname()),head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),dirty=subprocess.check_output(['git','status','--short'],cwd=ROOT,text=True),source=inventory,manifest_hashes=manifests,environment={k:os.environ.get(k) for k in ['DISPLAY','WAYLAND_DISPLAY','SDL_VIDEODRIVER']},target='v1 full quality, vsync0 cap0',warmup_ticks=warmup,measure_ticks=ticks-warmup,repeats=a.repeats,assets={str(f.relative_to(ROOT.parent)):digest(f) for folder in [ROOT.parent/'realm-client/src/kabam/rotmg/assets',ROOT.parent/'realm-client/src/com/company/ui/fonts'] for f in folder.rglob('*') if f.is_file()},map=digest(ROOT.parent/'realm-server/Resources/Worlds/Nexus/0.jm'),binaries={str(x):digest(x) for x in [a.viewer.resolve()]+([a.compare.resolve()] if a.compare else [])}))
    runs=[];expected={}
    for name in names:
        for repeat in range(a.repeats):
            variants=[('current',a.viewer.resolve())]+([('compare',a.compare.resolve())] if a.compare else [])
            random.Random(77+repeat).shuffle(variants)
            for variant,viewer in variants:
                stem=f'{name}-{repeat}-{variant}';summary=out/(stem+'.json');raw=out/(stem+'.jsonl');log=out/(stem+'.log')
                cmd=[str(viewer),'--benchmark','--stress',str(FIXTURES/index['scenarios'][name]),'--stress-summary',str(summary),'--frames',str(ticks),'--width',str(a.width),'--height',str(a.height),'--telemetry',str(raw),'--telemetry-warmup',str(warmup)]
                if a.trace:cmd+=['--trace',str(out/(stem+'.trace.json'))]
                if not a.visible:cmd+=['--hidden']
                if a.capture:cmd+=['--capture',str(out/(stem+'.png'))]
                start=time.monotonic();result=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=a.timeout);log.write_text(result.stdout)
                assert result.returncode==0,result.stdout
                state=json.loads(summary.read_text());signature={k:state[k] for k in ['state_checksum','packet_checksum','packets','hits','ticks','resident','simulated_entity_steps','spawned','removed','resets']}
                if name in expected:assert signature==expected[name],f'Determinism regression: {name}'
                expected[name]=signature
                row=dict(scenario=name,repeat=repeat,variant=variant,command=cmd,wall_seconds=time.monotonic()-start,summary=state,metrics=report(raw),raw=str(raw),log=str(log),capture=str(out/(stem+'.png')) if a.capture else None)
                runs.append(row);write(out/'results.json',dict(version=1,runs=runs));print(stem,'CPU p95',row['metrics']['cpu_ms']['p95'],'hits',state['hits'],'resident',state['resident'],flush=True)
    if a.with_peers:
        subprocess.run(['python3',str(ROOT/'tests/stress_packets_tests.py'),str(a.driver.resolve()),str(out/'packets')],cwd=ROOT,check=True)
        subprocess.run(['ctest','--test-dir',str(a.viewer.resolve().parent),'-R','^(skins_http|guild_board_http|guild_members_http)$','--output-on-failure'],cwd=ROOT,check=True)
    summarize(out,runs,ticks,warmup,a.repeats,bool(a.compare),a.plot)
if __name__=='__main__':main()
