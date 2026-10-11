#!/usr/bin/env python3
"""Bake finite Flash HUD art into an indexed PNG bundle for native runtime."""
import argparse
import json
from pathlib import Path
import subprocess
from build_flash_reference import build

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--flash-root', type=Path, default=Path(__file__).resolve().parents[2] / 'realm-client')
p.add_argument('--sdk', type=Path, default=Path.home() / 'Desktop/AIRSDK_Linux')
p.add_argument('--java-home', type=Path, default=Path.home() / '.local/share/air-jdk')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.build = a.build.resolve(); a.output = a.output.resolve()
reference = a.build / 'flash-inventory-reference'
descriptor = build('InventoryExporter', a.flash_root.resolve(), a.sdk.resolve(), a.java_home.resolve(), reference)
raw = a.output / 'raw'; raw.mkdir(parents=True, exist_ok=True)
tasks = a.output / 'tasks.json'; tasks.write_text(json.dumps(dict(output=str(raw), bake=True)))
subprocess.run([str(a.sdk / 'bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=180)
images = []
with (a.output / 'images.bin').open('wb') as bundle:
    for key in json.loads((raw / 'entries.json').read_text()):
        data = (raw / (key + '.png')).read_bytes()
        images.append(dict(key=key, offset=bundle.tell(), length=len(data)))
        bundle.write(data)
source = json.loads((a.build / 'generated/source_inventory.json').read_text())
manifest = dict(schema=1, source_sha256=source['source_sha256'], images=images)
(a.output / 'manifest.json').write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
print(f"Baked {len(images)} UI records into {a.output / 'images.bin'}")
