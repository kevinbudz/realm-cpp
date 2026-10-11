#!/usr/bin/env python3
"""Real class skin list/modal/detail captures vs native at playing sizes."""
import json,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build/skin-reference';out.mkdir(parents=True,exist_ok=True)
cases=json.loads((ROOT/'tests/skin_cases.json').read_text());inputs=out/'input.json';inputs.write_text(json.dumps(dict(output=str(out/'flash'),cases=cases)))
subprocess.run(['python3',str(ROOT/'tools/build_flash_reference.py'),'SkinsExporter','--output',str(out/'exporter')],check=True,cwd=ROOT)
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(out/'exporter/SkinsExporter-app.xml'),'--',str(inputs)],check=True,timeout=50,cwd=ROOT)
subprocess.run([str(ROOT/'build/realm_skin_inputs'),str(ROOT/'build/fonts-client'),str(ROOT/'build/hud-art'),str(ROOT/'tests/skin_cases.json'),str(out/'native')],check=True,cwd=ROOT)
a=json.loads((out/'flash/metrics.json').read_text());b=json.loads((out/'native/metrics.json').read_text());assert a==b,(a,b)
for c in cases:
 w,h=c['width'],c['height'];pair=Image.new('RGB',(w*2,h+24));draw=ImageDraw.Draw(pair)
 for j,k in enumerate(['flash','native']):pair.paste(Image.open(out/k/(c['key']+'.png')),(w*j,24));draw.text((w*j+10,5),k+': '+c['key'],fill='white')
 pair.save(out/('comparison-'+c['key']+'.png'))
print(f'{len(cases)} real Flash/native skin geometry checks passed and pairs generated')
