#!/usr/bin/env python3
"""Original embedded shader/TextureRedrawer vs native bitmap composition."""
import json,subprocess,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageChops
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build/dye-reference';out.mkdir(parents=True,exist_ok=True)
cases=[]
def add(file,index,dir=0,action=0,phase=0,size=100,pad=False,tex1=0,tex2=0,animated=True,**extra):
 cases.append(dict(key=f'case-{len(cases):03}',file=file,index=index,dir=dir,action=action,phase=phase,size=size,pad=pad,tex1=tex1,tex2=tex2,animated=animated,**extra))
for file,index in [('players',0),('players',2),('players',7),('playerskins',0),('playerskins',1)]:
 for size in [50,100,137,200,350]:add(file,index,size=size,tex1=0x1ffdc85,tex2=0x15090ff)
 for dir in range(4):
  for action,phase in [(0,0),(1,.75),(2,.75)]:add(file,index,dir,action,phase,tex1=0x4000031,tex2=0x9000007)
 for kind in [4,5,9,10]:add(file,index,size=150,tex1=(kind<<24)|7,tex2=(kind<<24)|8)
 for a,b in [(0,0),(0x1000000,0),(0,0x1ffffff),(0x1ffffff,0x1000000)]:add(file,index,size=50,pad=True,tex1=a,tex2=b)
add('lofiObj',0,size=125,tex1=0x1ff0000,animated=False)
for size in [50,100,137,200]:add('lofiObj3',16,size=size,tex1=0x1ff0000,animated=False,maskFile='lofiObj3',maskIndex=24)
(out/'cases.json').write_text(json.dumps(cases));(out/'input.json').write_text(json.dumps(dict(output=str(out/'flash'),cases=cases)))
if '--reuse' not in sys.argv: subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'DyesExporter','--output',str(out/'exporter')],check=True,cwd=ROOT)
if '--reuse' not in sys.argv: subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(out/'exporter/DyesExporter-app.xml'),'--',str(out/'input.json')],check=True,timeout=80,cwd=ROOT)
subprocess.run([str(ROOT/'build/realm_dye_inputs'),str(out/'cases.json'),str(out/'flash'),str(out/'native')],check=True,cwd=ROOT)
a=json.loads((out/'flash/metrics.json').read_text());b=json.loads((out/'native/metrics.json').read_text());assert a==b,(a,b)
rows=[]
for c in cases:
 f=Image.open(out/'flash'/(c['key']+'.png'));n=Image.open(out/'native'/(c['key']+'.png'));w,h=f.size
 # Core texture stays opaque and carries the exact textile/shading formula.
 errors=[]
 for y in range(12,h-(12 if c['pad'] else 1)):
  for x in range(12,w-12):
   p,q=f.getpixel((x,y)),n.getpixel((x,y))
   if p[3]==q[3]==255 and max(abs(p[k]-q[k]) for k in range(3))>4:
    # Flash and native nearest sampling may round a boundary differently.
    # Verify opaque interiors; keep boundary/halo differences visible in pairs.
    neighborhood=[f.getpixel((xx,yy)) for yy in range(max(0,y-1),min(h,y+2)) for xx in range(max(0,x-1),min(w,x+2))]
    if all(v[3]==255 and max(abs(v[k]-p[k]) for k in range(3))<=3 for v in neighborhood):errors.append((x,y,p,q))
 assert not errors,(c,errors[:5])
 pair=Image.new('RGB',(w*2,h+20),(54,54,54));pair.paste(f,(0,20),f);pair.paste(n,(w,20),n);ImageDraw.Draw(pair).text((2,2),c['key'],fill='white');pair.save(out/('comparison-'+c['key']+'.png'))
 rows.append(pair)
contact=Image.new('RGB',(1000,((len(rows)+5)//6)*200),(36,36,36));d=ImageDraw.Draw(contact)
for i,im in enumerate(rows):
 im.thumbnail((162,175));contact.paste(im,((i%6)*166,(i//6)*200));d.text(((i%6)*166,(i//6)*200+177),cases[i]['file']+str(cases[i]['index']),fill='white')
contact.save(out/'contact.png');print(f'{len(cases)} actual Flash/native dye/shader dimensions and opaque core checks passed')
