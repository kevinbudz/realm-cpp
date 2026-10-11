#!/usr/bin/env python3
"""Source Player.getPortrait in real group rows, normal and resized text."""
from pathlib import Path
import json,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/dye-reference/ui';OUT.mkdir(parents=True,exist_ok=True)
cases=[]
for scale in [1,1.2,2,3.6]:
 for skin,tex1,tex2 in [(0,0x1ff0000,0x10000ff),(836,0x4000007,0x5000008),(836,0x9000007,0xa000008)]:
  for menu in [False,True]:
   cases.append(dict(id=f'ui-{len(cases):02}',scale=scale,menu=menu,teleport=True,hover=False,cooldown=0,players=[dict(type=782,name='Dyed Wizard',level=20,fellowGuild=True,skin=skin,tex1=tex1,tex2=tex2),dict(type=775,name='Dyed Archer',level=18,fellowGuild=False,tex1=tex2,tex2=tex1)]))
(OUT/'cases.json').write_text(json.dumps(cases));(OUT/'input.json').write_text(json.dumps(dict(output=str(OUT/'flash'),cases=cases)))
subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'PlayerGroupExporter','--output',str(OUT/'exporter')],check=True)
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(OUT/'exporter/PlayerGroupExporter-app.xml'),'--',str(OUT/'input.json')],check=True,timeout=35)
subprocess.run([str(ROOT/'build/realm_player_group_inputs'),str(ROOT/'build/fonts-client'),str(ROOT/'build/hud-art'),str(OUT/'cases.json'),str(OUT/'native')],check=True)
a=json.loads((OUT/'flash/metrics.json').read_text());b=json.loads((OUT/'native/metrics.json').read_text());assert a==b,(a,b)
for c in cases:
 ims=[Image.open(OUT/k/(c['id']+'.png')) for k in ['flash','native']];w=max(im.width for im in ims);h=max(im.height for im in ims);pair=Image.new('RGB',(w*2,h+24),(55,55,55));d=ImageDraw.Draw(pair)
 for j,im in enumerate(ims):
  bg=Image.new('RGBA',im.size,(80,80,80,255));bg.alpha_composite(im);pair.paste(bg.convert('RGB'),(j*w,24));d.text((j*w+4,4),['Flash','Native'][j]+': '+c['id'],fill='white')
 pair.save(OUT/('comparison-'+c['id']+'.png'))
print(f'{len(cases)} real Flash/native dyed group row/skin/portrait/menu fixtures with matched frames passed')
