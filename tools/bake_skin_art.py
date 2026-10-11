#!/usr/bin/env python3
"""Add finite original skin icons/portraits/previews beside existing HUD artwork."""
import json,subprocess
from pathlib import Path
from build_flash_reference import build
ROOT=Path(__file__).resolve().parents[1]
output=ROOT/'build/hud-art/skins';raw=output/'raw';raw.mkdir(parents=True,exist_ok=True)
work=ROOT/'build/skins-art-exporter'
descriptor=build('SkinsExporter',ROOT.parent/'realm-client',Path.home()/'Desktop/AIRSDK_Linux',Path.home()/'.local/share/air-jdk',work)
inputs=work/'input.json';inputs.write_text(json.dumps(dict(art=str(raw),output=str(work/'unused'),cases=[])))
subprocess.run([str(Path.home()/'Desktop/AIRSDK_Linux/bin/adl'),'-nodebug',str(descriptor),'--',str(inputs)],check=True,timeout=90)
images=[]
with (output/'images.bin').open('wb') as bundle:
 for key in json.loads((raw/'entries.json').read_text()):
  data=(raw/(key+'.png')).read_bytes();images.append(dict(key=key,offset=bundle.tell(),length=len(data)));bundle.write(data)
source=json.loads((ROOT/'build/generated/source_inventory.json').read_text())
(output/'manifest.json').write_text(json.dumps(dict(schema=1,source_sha256=source['source_sha256'],images=images)))
print(f'{len(images)} original skin UI records baked in a separate supplement')
