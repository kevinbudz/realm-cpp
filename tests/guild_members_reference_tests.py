#!/usr/bin/env python3
import json,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build/guild-members-reference';out.mkdir(parents=True,exist_ok=True)
cases=ROOT/'tests/guild_members_cases.json';art=ROOT/'build/hud-art/guild-members'
inputs=out/'flash-input.json';inputs.write_text(json.dumps(dict(output=str(out/'flash'),art=str(art),cases=json.loads(cases.read_text()))))
subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'GuildMembersExporter','--output',str(out/'exporter')],cwd=ROOT,check=True)
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(out/'exporter/GuildMembersExporter-app.xml'),'--',str(inputs)],cwd=ROOT,check=True,timeout=45)
subprocess.run([str(ROOT/'build/realm_guild_members_inputs'),str(ROOT/'build/fonts-client'),str(ROOT.parent/'realm-client'),str(art),str(cases),str(out/'native')],check=True)
a=json.loads((out/"flash/metrics.json").read_text());b=json.loads((out/"native/metrics.json").read_text())
for x,y in zip(a,b,strict=True):assert x==y,(x,y)
for c in json.loads(cases.read_text()):
 w,h=c['width'],c['height'];s=h/600;x=(w-800*s)/2;box=(int(max(0,x)),0,int(min(w,x+800*s)),h)
 cw=box[2]-box[0];pair=Image.new('RGB',(cw*2,h+24),(80,80,80));draw=ImageDraw.Draw(pair)
 for j,kind in enumerate(['flash','native']):
  im=Image.open(out/kind/(c['key']+'.png'));assert im.size==(w,h)
  pair.paste(im.convert('RGB').crop(box),(cw*j,24));draw.text((cw*j+5,5),kind+': '+c['key'],fill='white')
 pair.save(out/('comparison-'+c['key']+'.png'))
print(f'{len(a)} real Flash/native guild member geometry checks and fixtures captured; original icons exported separately')
