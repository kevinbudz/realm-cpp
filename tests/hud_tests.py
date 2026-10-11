#!/usr/bin/env python3
"""Check panel reconstruction, incremental atlas uploads, and source fidelity."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageChops
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('fonts', type=Path)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--native-only', action='store_true', help='Check CPU/GPU consistency without claiming source parity')
p.add_argument('--cases', type=int, nargs='+')
a = p.parse_args(); a.build = a.build.resolve(); a.fonts = a.fonts.resolve()
directory = a.build / 'hud-reference'; directory.mkdir(parents=True, exist_ok=True)
fixtures = [(1,0,50,100,100,0,80,100,0), (4,123,500,73,345,0,17,100,0),
            (20,1250,2000,0,720,0,0,385,0), (20,850,1000,765,865,145,385,385,0),
            (20,20,100,800,865,150,300,450,65), (2,25,50,40,100,-10,1,100,-20),
            (10,173,1234,123,456,0,22,567,0)]
cases = []
for index, (level, xp, xpmax, hp, hpmax, hpboost, mp, mpmax, mpboost) in enumerate(fixtures):
    bars = [dict(label='Fame' if level==20 else f'Lvl {level}', color=14835456 if level==20 else 5931045,
                 y=230, value=xp, maximum=xpmax, boost=0, cap=-1),
            dict(label='HP', color=14693428, y=254, value=hp, maximum=hpmax, boost=hpboost, cap=720),
            dict(label='MP', color=6325472, y=278, value=mp, maximum=mpmax, boost=mpboost, cap=385)]
    cases.append(dict(id=str(index), name='NativeTest', bars=bars))
tasks = directory / 'tasks.json'; tasks.write_text(json.dumps(dict(output=str(directory / 'flash'), cases=cases)))
subprocess.run([str(a.build / 'realm_hud_inputs'), str(a.fonts), str(tasks)], check=True)
subprocess.run([str(a.build / 'realm_hud_gpu_inputs'), str(a.fonts), str(tasks)], check=True)
if not a.native_only:
    descriptor = a.build / 'flash-hud-reference/HudExporter-app.xml'
    if not descriptor.exists():
        subprocess.run(['python3', str(Path(__file__).resolve().parents[1] / 'tools/build_flash_reference.py'),
                        'HudExporter', '--output', str(descriptor.parent)], check=True)
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), str(descriptor), '--', str(tasks)], check=True, timeout=60)
failures = []
for case in cases:
    relative = case['id'] + '.png'
    native = Image.open(directory / 'native' / relative).convert('RGBA')
    gpu = Image.open(directory / 'native-gpu' / relative).convert('RGBA')
    if native.tobytes() != gpu.tobytes():
        raise AssertionError(f'Incremental GPU panel {relative} differs from CPU panel')
    if not a.native_only and (a.cases is None or int(case['id']) in a.cases):
        expected = Image.open(directory / 'flash' / relative).convert('RGBA')
        diff = ImageChops.difference(expected, native)
        if diff.getbbox(alpha_only=False):
            diff.save(directory / 'native' / (case['id'] + '-difference.png'))
            failures.append((case['id'], sum(any(pixel) for pixel in diff.get_flattened_data()), diff.getbbox(alpha_only=False)))
(directory / 'comparison.json').write_text(json.dumps(dict(cpu_gpu_cases=len(cases), source_cases=a.cases if a.cases is not None else list(range(len(cases))), source_failures=failures, native_only=a.native_only), indent=2) + '\n')
if failures:
    raise AssertionError(f'Source panel differences: {failures}')
print(f'{len(cases)} incrementally uploaded GPU panels match native reconstruction exactly')
if not a.native_only:
    print(f'{len(a.cases) if a.cases is not None else len(cases)} selected panels match original Flash exactly')
