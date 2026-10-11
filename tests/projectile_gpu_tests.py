#!/usr/bin/env python3
"""Compare native projectile geometry with original Point3D on Flash Stage3D."""
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
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
p.add_argument('--java-home', type=Path, default=Path.home() / '.local/share/air-jdk')
a = p.parse_args()
a.processed = a.processed.resolve(); a.build = a.build.resolve()
reference = a.build / 'flash-gpu-projectiles'
build('StageTextureExporter', Path(__file__).resolve().parents[2] / 'realm-client', a.sdk.resolve(), a.java_home.resolve(), reference)
source = json.loads((a.processed / 'tasks.json').read_text())
selected, seen = [], set()
for task in source:
    if not task['padBottom']:
        continue
    with Image.open(task['input']) as image:
        signature = (image.size, task['size'])
    if signature in seen:
        continue
    seen.add(signature); selected.append(task)
    if len(selected) == 6:
        break
assert selected, 'Bake projectile textures before running this test'
tasks, captures = [], []
for index, task in enumerate(selected):
    for outline in (False, True):
        for rotation, angle in enumerate((0, .37, 1.5707963267948966, 3.02)):
            name = f'{index}-{int(outline)}-{rotation}.png'
            tasks.append(dict(input=task['input'], output=str(reference / 'flash' / name), processed=not outline, padBottom=outline,
                              size=task['size'], projectile=True, outline=outline, angle=angle))
            # In the unoutlined case both renderers sample the original PNG.
            captures.append((name, task['output'] if outline else task['input'], task['size'], angle, outline))
# Cover every 8-bit alpha value with colored pixels, including the loader's
# premultiplication quantization. Black outline pixels alone cannot reveal it.
alpha_image = Image.new('RGBA', (16, 16))
alpha_image.putdata([((alpha * 17 + 13) % 256, (alpha * 31 + 29) % 256, (alpha * 47 + 41) % 256, alpha) for alpha in range(256)])
alpha_path = reference / 'raw-alpha.png'; alpha_image.save(alpha_path)
tasks.append(dict(input=str(alpha_path), output=str(reference / 'flash/raw-alpha.png'), processed=True))
(reference / 'tasks.json').write_text(json.dumps(tasks))
subprocess.run([str(a.sdk / 'bin/adl'), str(reference / 'StageTextureExporter-app.xml'), str(reference), '--', str(reference / 'tasks.json')], check=True, timeout=60)
for name, texture, size, angle, outline in captures:
    capture = reference / 'native' / name
    command = [str(a.build / 'realm_viewer'), '--texture', texture, '--projectile-angle', str(angle), '--projectile-size', str(size),
               '--hidden', '--benchmark', '--frames', '1', '--capture', str(capture)]
    if outline:
        command.append('--projectile-outline')
    else:
        command.append('--raw-flash-bitmap')
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, timeout=10)
    with Image.open(reference / 'flash' / name) as flash, Image.open(capture) as native:
        flash, native = flash.convert('RGBA'), native.convert('RGBA')
        if flash.size != native.size or flash.tobytes() != native.tobytes():
            ImageChops.difference(flash, native).save(reference / ('difference-' + name))
            raise AssertionError(f'Point3D/native pixel mismatch: {name}')
print(f'{len(captures)} outlined/unoutlined projectile frames match all 480,000 Stage3D pixels exactly')
alpha_capture = reference / 'native/raw-alpha.png'
subprocess.run([str(a.build / 'realm_viewer'), '--texture', str(alpha_path), '--raw-flash-bitmap', '--hidden', '--frames', '1', '--capture', str(alpha_capture)],
               check=True, stdout=subprocess.DEVNULL, timeout=10)
with Image.open(reference / 'flash/raw-alpha.png') as flash, Image.open(alpha_capture) as native:
    assert flash.convert('RGBA').tobytes() == native.convert('RGBA').tobytes(), 'Flash colored alpha upload/blend mismatch'
print('All 256 raw BitmapData alpha values match the Stage3D framebuffer exactly')
