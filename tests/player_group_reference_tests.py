#!/usr/bin/env python3
"""Real Flash classes versus native group layout and drawable-density text."""
from pathlib import Path
import json,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/player-group-reference';OUT.mkdir(parents=True,exist_ok=True)
cases=ROOT/'tests/player_group_cases.json'
(OUT/'input.json').write_text(json.dumps(dict(output=str(OUT/'flash'),cases=json.loads(cases.read_text()))))
subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'PlayerGroupExporter','--output',str(OUT/'exporter')],check=True)
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(OUT/'exporter/PlayerGroupExporter-app.xml'),'--',str(OUT/'input.json')],check=True,timeout=30)
subprocess.run([str(ROOT/'build/realm_player_group_inputs'),str(ROOT/'build/fonts-client'),str(ROOT/'build/hud-art'),str(cases),str(OUT/'native')],check=True)
a=json.loads((OUT/'flash/metrics.json').read_text());b=json.loads((OUT/'native/metrics.json').read_text());assert a==b,(a,b)
for c in json.loads(cases.read_text()):
 x=Image.open(OUT/'flash'/(c['id']+'.png'));y=Image.open(OUT/'native'/(c['id']+'.png'))
 # Source bounds include the one-pixel outline; native reserves logical frame+44.
 assert abs(x.width-y.width)<=4 and abs(x.height-y.height)<=4
 w=max(x.width,y.width);h=max(x.height,y.height)
 pair=Image.new('RGB',(w*2,h+24),(55,55,55));d=ImageDraw.Draw(pair)
 for j,(label,im) in enumerate([('Flash',x),('Native',y)]):
  bg=Image.new('RGBA',im.size,(80,80,80,255));bg.alpha_composite(im);pair.paste(bg.convert('RGB'),(j*w,24));d.text((j*w+4,4),label+': '+c['id'],fill='white')
 pair.save(OUT/('comparison-'+c['id']+'.png'))
print(f'{len(a)} real Flash/native tooltip/menu/hover/cooldown/restricted-map layout fixtures match')
