#!/usr/bin/env python3
"""Real Flash board classes vs native; geometry checks and visual pairs."""
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/guild-board-reference'
out.mkdir(parents=True,exist_ok=True)
cases=json.loads((ROOT/'tests/guild_board_cases.json').read_text())
inputs=out/'flash-input.json'
inputs.write_text(json.dumps({'output':str(out/'flash'),'cases':cases}))
subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'GuildBoardExporter','--output',str(out/'exporter')],cwd=ROOT,check=True)
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(out/'exporter/GuildBoardExporter-app.xml'),'--',str(inputs)],cwd=ROOT,check=True,timeout=45)
subprocess.run([str(ROOT/'build/realm_guild_board_inputs'),str(ROOT/'build/fonts-client'),str(ROOT.parent/'realm-client'),str(ROOT/'tests/guild_board_cases.json'),str(out/'native')],cwd=ROOT,check=True)
a=json.loads((out/'flash/metrics.json').read_text());b=json.loads((out/'native/metrics.json').read_text())
for x,y in zip(a,b,strict=True):
 assert x==y,(x,y)
for c in cases:
 w,h=c['width'],c['height'];s=h/600
 x=(w-426*s)/2-12*s;y=(h-432*s)/2-12*s
 box=(int(x),int(y),int(x+450*s),int(y+460*s))
 pair=Image.new('RGB',(int(450*s)*2,int(460*s)+24),(80,80,80));draw=ImageDraw.Draw(pair)
 for j,kind in enumerate(['flash','native']):
  pair.paste(Image.open(out/kind/(c['key']+'.png')).convert('RGB').crop(box),(int(450*s)*j,24))
  draw.text((int(450*s)*j+5,5),kind+': '+c['key'],fill='white')
 pair.save(out/('comparison-'+c['key']+'.png'))
print(f'{len(cases)} real Flash/native guild board geometry checks passed; visual pairs generated')
