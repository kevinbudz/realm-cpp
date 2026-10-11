#!/usr/bin/env python3
"""Render actual Flash effect classes; review native Scene snapshots on the same states.
Randomized geometry is shared only for visual comparison. Native event simulation
is independently covered by particle_tests and actual TCP/server fixtures.
"""
from pathlib import Path
import json,subprocess,sys
from PIL import Image,ImageDraw,ImageChops
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build/particle-reference';out.mkdir(parents=True,exist_ok=True)
cases=[]
for scale in [1,2]:
 for kind in list(range(1,14))+list(range(16,26)):
  for time in ([68,170] if kind in [5,8,10,12,13,17] else [170,680] if kind not in [6,18,19] else [68,170]):
   cases.append(dict(id=f'effect-{kind:02}-{time}-{scale}',type=kind,time=time,scale=scale))
(out/'input.json').write_text(json.dumps(dict(output=str(out/'flash'),cases=cases)))
if '--reuse' not in sys.argv:
 subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'ParticlesExporter','--output',str(out/'exporter')],check=True,cwd=ROOT,stdout=(out/'compile.log').open('w'),stderr=subprocess.STDOUT)
 subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(out/'exporter/ParticlesExporter-app.xml'),'--',str(out/'input.json')],check=True,timeout=60,cwd=ROOT)
subprocess.run([str(ROOT/'build/realm_particle_inputs'),str(out/'flash/states.json'),str(out/'native')],check=True,cwd=ROOT)
states=json.loads((out/'flash/states.json').read_text());contact=Image.new('RGB',(1200,((len(cases)+3)//4)*175),(40,40,40));d=ImageDraw.Draw(contact)
for i,c in enumerate(cases):
 f=Image.open(out/'flash'/(c['id']+'.png')).convert('RGB');n=Image.open(out/'native'/(c['id']+'.png')).convert('RGB');assert f.size==n.size
 fb=ImageChops.difference(f,Image.new('RGB',f.size,(54,54,54))).getbbox();nb=ImageChops.difference(n,Image.new('RGB',n.size,(54,54,54))).getbbox()
 if fb:
  assert nb,c
  # Missing/stale effect geometry is a defect. Small filter/interior sampling
  # differences are retained in captures for normal-size review.
  assert max(abs(a-b) for a,b in zip(fb,nb))<=5*c['scale'],(c,fb,nb)
 pair=Image.new('RGB',(f.width*2,f.height+22),(40,40,40));pair.paste(f,(0,22));pair.paste(n,(f.width,22));ImageDraw.Draw(pair).text((5,3),c['id']+' Flash / native',fill='white');pair.save(out/('comparison-'+c['id']+'.png'))
 crop=pair.crop((120*c['scale'],22+90*c['scale'],600*c['scale']+f.width,22+510*c['scale']));crop.thumbnail((298,150));contact.paste(crop,((i%4)*300,(i//4)*175+20));d.text(((i%4)*300+3,(i//4)*175+2),c['id'],fill='white')
contact.save(out/'contact.png');print(f'{len(cases)} actual Flash particle-class/native Scene frames at normal/resized size passed geometry/presence checks; compare captures for filters')
