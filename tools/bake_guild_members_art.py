#!/usr/bin/env python3
"""Bake the finite original guild chronicle icons beside the existing HUD pack."""
import argparse,json,subprocess
from pathlib import Path
from build_flash_reference import build
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'build/hud-art/guild-members');p.add_argument('--sdk',type=Path,default=Path.home()/'Desktop/AIRSDK_Linux');p.add_argument('--flash-root',type=Path,default=ROOT.parent/'realm-client');p.add_argument('--java-home',type=Path,default=Path.home()/'.local/share/air-jdk');a=p.parse_args()
work=ROOT/'build/guild-members-art-exporter';descriptor=build('GuildMembersExporter',a.flash_root.resolve(),a.sdk.resolve(),a.java_home.resolve(),work)
inputs=work/'input.json';inputs.write_text(json.dumps(dict(art=str(a.output.resolve()),output=str(work/'unused'),cases=[])))
subprocess.run([str(a.sdk/'bin/adl'),'-nodebug',str(descriptor),'--',str(inputs)],check=True,timeout=45)
print(f'Original guild fame/rank/remove art saved to {a.output.resolve()}; existing HUD bundle unchanged')
