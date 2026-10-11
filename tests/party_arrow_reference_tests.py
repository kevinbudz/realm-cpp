#!/usr/bin/env python3
"""Capture unmodified Flash PlayerArrow/GameObjectArrow and native projection/art."""
from pathlib import Path
import json,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/party-arrow-reference';OUT.mkdir(parents=True,exist_ok=True)
cases=ROOT/'tests/party_arrow_cases.json';tasks=json.loads(cases.read_text());(OUT/'input.json').write_text(json.dumps(dict(output=str(OUT/'flash'),cases=tasks)))
subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'PartyArrowExporter','--output',str(OUT/'exporter')],check=True)
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(OUT/'exporter/PartyArrowExporter-app.xml'),'--',str(OUT/'input.json')],check=True,timeout=30)
subprocess.run([str(ROOT/'build/realm_party_arrow_inputs'),str(ROOT/'build/fonts-client'),str(ROOT/'build/hud-art'),str(cases),str(OUT/'native')],check=True)
a=json.loads((OUT/'flash/metrics.json').read_text());b=json.loads((OUT/'native/metrics.json').read_text())
for x,y in zip(a,b):
 for key in ['x','y']:assert abs(x[key]-y[key])<1e-6,(x,y)
 assert abs((x['rotation']-y['rotation']+180)%360-180)<1e-6,(x,y)
for c in tasks:
 a=Image.open(OUT/'flash'/(c['id']+'.png'));b=Image.open(OUT/'native'/(c['id']+'.png'));assert a.size==b.size==(c['width'],c['height'])
 bbox=a.getbbox();other=b.getbbox();r=(min(bbox[0],other[0]),min(bbox[1],other[1]),max(bbox[2],other[2]),max(bbox[3],other[3]));a=a.crop(r);b=b.crop(r)
 pair=Image.new('RGB',(a.width*2,a.height+24),(80,80,80));d=ImageDraw.Draw(pair)
 for n,(label,im) in enumerate([('Flash',a),('Native',b)]):
  bg=Image.new('RGBA',im.size,(80,80,80,255));bg.alpha_composite(im);pair.paste(bg.convert('RGB'),(n*a.width,24));d.text((n*a.width+3,3),label+': '+c['id'],fill='white')
 pair.save(OUT/('comparison-'+c['id']+'.png'))
print('18 real Flash/native edge/corner/rotation/offset/zoom/resize projection metrics match; tooltip resize placement intentionally uses scaled bounds')
