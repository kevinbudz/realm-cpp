#!/usr/bin/env python3
"""Compare baked native sprites against real Flash Stage3D draws of the original filters."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageChops
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_flash_reference import build
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('processed', type=Path)
p.add_argument('--include-projectiles', action='store_true', help='Also test padded projectile bitmaps; geometry has a separate test')
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
p.add_argument('--java-home', type=Path, default=Path.home() / '.local/share/air-jdk')
a = p.parse_args(); a.processed = a.processed.resolve(); a.build = a.build.resolve()
reference = a.build / 'flash-gpu-reference'; reference.mkdir(parents=True, exist_ok=True)
flash_root = Path(__file__).resolve().parents[2] / 'realm-client'
build('StageTextureExporter', flash_root, a.sdk.resolve(), a.java_home.resolve(), reference)
source_tasks = json.loads((a.processed / 'tasks.json').read_text())
if not a.include_projectiles:
    source_tasks = [task for task in source_tasks if not task['padBottom']]
tasks = []
for task in source_tasks:
    tasks.append(dict(input=task['input'], output=str(reference / 'flash' / Path(task['output']).name), size=task['size'], padBottom=task['padBottom'], processed=False))
(reference / 'tasks.json').write_text(json.dumps(tasks))
subprocess.run([str(a.sdk / 'bin/adl'), str(reference / 'StageTextureExporter-app.xml'), str(reference), '--', str(reference / 'tasks.json')], check=True, timeout=60)
for task in source_tasks:
    name = Path(task['output']).name
    capture = reference / 'native' / name
    subprocess.run([str(a.build / 'realm_viewer'), '--texture', task['output'], '--hidden', '--benchmark', '--frames', '1', '--capture', str(capture)], check=True, stdout=subprocess.DEVNULL, timeout=10)
    source = Image.open(reference / 'flash' / name).convert('RGBA')
    native = Image.open(capture).convert('RGBA')
    difference = ImageChops.difference(source, native)
    if source.size != native.size or difference.getbbox(alpha_only=False):
        difference.save(reference / ('difference-' + name))
        raise AssertionError(f'Stage3D/native pixel mismatch: {name}')
print(f'{len(tasks)} filtered sprites match all 480,000 Stage3D framebuffer pixels exactly')
