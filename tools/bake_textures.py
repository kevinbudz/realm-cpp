#!/usr/bin/env python3
"""Bake original Flash texture filters once; the native viewer needs only PNGs."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--flash-root', type=Path, default=Path(__file__).resolve().parents[2] / 'realm-client')
p.add_argument('--sdk', type=Path, default=Path(os.environ.get('AIR_SDK', str(Path.home() / 'Desktop/AIRSDK_Linux'))))
p.add_argument('--java-home', type=Path, default=Path(os.environ.get('JAVA_HOME', str(Path.home() / '.local/share/air-jdk'))))
p.add_argument('--output', type=Path, required=True)
p.add_argument('maps', nargs='+', help='Map paths, or "all" for all object definitions')
a = p.parse_args()
a.build = a.build.resolve(); a.output = a.output.resolve(); a.flash_root = a.flash_root.resolve(); a.sdk = a.sdk.resolve()
reference = a.build / 'flash-reference'; reference.mkdir(parents=True, exist_ok=True)
from build_flash_reference import build
subprocess.run([str(a.build / 'realm_bake_inputs'), str(a.output)] + ['--all' if x == 'all' else x for x in a.maps], check=True)
build('TextureExporter', a.flash_root, a.sdk, a.java_home.resolve(), reference)
subprocess.run([str(a.sdk / 'bin/adl'), str(reference / 'TextureExporter-app.xml'), str(reference), '--', str(a.output / 'tasks.json')], check=True)
manifest = json.loads((a.output / 'manifest.json').read_text())
missing = [entry['file'] for entry in manifest['images'] if not (a.output / entry['file']).is_file()]
if missing:
    raise RuntimeError(f'Exporter exited without producing {len(missing)} textures')
print(f"Baked {len(manifest['images'])} Flash reference textures into {a.output}")
