#!/usr/bin/env python3
"""Checks production timeline invariants and determinism under changed render pacing."""
import json,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tests/fixtures'
KEYS=['state_checksum','packet_checksum','packets','hits','resident','simulated_entity_steps','spawned','removed','resets','particles','optional_drops']
def main():
    driver=Path(sys.argv[1]).resolve();viewer=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
    with tempfile.TemporaryDirectory() as tmp:
        out=Path(tmp);manifest=json.loads((FIX/'determinism.json').read_text());manifest['reset_every']=40;(out/'small.json').write_text(json.dumps(manifest))
        def run(name,exe,delay):
            summary=out/(name+'.json')
            if exe==driver:cmd=[str(exe),str(out/'small.json'),'100',str(summary),str(delay),str(out/(name+'.jsonl'))]
            else:cmd=[str(exe),'--hidden','--benchmark','--stress',str(out/'small.json'),'--stress-summary',str(summary),'--frames','100','--stress-delay',str(delay),'--telemetry',str(out/(name+'.jsonl'))]
            r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stdout+r.stderr
            return json.loads(summary.read_text())
        a=run('headless',driver,0);b=run('headless-paced',driver,2)
        assert {k:a[k] for k in KEYS}=={k:b[k] for k in KEYS}
        assert a['session_replay'] and a['replayed_packets']>20 and a['packets']['4']==20 and a['packets']['21']==3
        assert a['hits']>0 and a['packets']['29']>0 and a['packets']['31']>0
        assert a['resets']==2 and a['replacements']>0
        assert a['particles']<=8192 and a['emitters']<=1024
        assert a['named_players']==9 and a['actor_sizes']==[100,125,150]
        assert a['resident'][0]==9 and a['resident'][1]==8
        expected=json.loads((FIX/'expected-small.json').read_text());assert {k:a[k] for k in KEYS}==expected
        if viewer:
            c=run('rendered',viewer,0);d=run('rendered-paced',viewer,3)
            assert {k:a[k] for k in KEYS}=={k:c[k] for k in KEYS}=={k:d[k] for k in KEYS}
            assert c['rendered_frames']==100 and c['submitted_last'][0]==9 and c['submitted_last'][1]==8
            for name in ['rendered','rendered-paced']:
                rows=[json.loads(x) for x in (out/(name+'.jsonl')).read_text().splitlines()]
                assert all(r['dropped_zones']==0 for r in rows if r['type']=='frame')
                assert any(r['name']=='combat.target_query' and r['calls']>1 and r['aggregate'] for r in rows if r['type']=='zone')
                assert any(r['name']=='network.decode_dispatch' for r in rows if r['type']=='zone')
                assert any(r['counters']['packets_decoded']>0 for r in rows if r['type']=='frame')
                assert any(r['name']=='fixture.ui_input' for r in rows if r['type']=='zone')
        # Invalid inputs cannot silently scale down the fixture.
        for change in [{'version':2},{'projectiles':40001},{'skins':[65535]},{'arrangement':'fake'}]:
            (out/'bad.json').write_text(json.dumps(manifest|change));r=subprocess.run([str(driver),str(out/'bad.json'),'1',str(out/'bad-result.json')],capture_output=True);assert r.returncode!=0
    print('Stress v1: frozen state/hit/wire outcomes, paced/rendered equivalence, real combat, churn/reset, caps and invalid manifests passed')
if __name__=='__main__':main()
