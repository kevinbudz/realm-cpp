#!/usr/bin/env python3
"""Bake the original Flash Myriad text rasterization and metrics for native UI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from fontTools.ttLib import TTFont
from build_flash_reference import build
from compile_fonts import compile_pack

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--build', type=Path, default=Path(__file__).resolve().parents[1] / 'build')
p.add_argument('--sdk', type=Path, default=Path(os.environ.get('AIR_SDK', str(Path.home() / 'Desktop/AIRSDK_Linux'))))
p.add_argument('--java-home', type=Path, default=Path(os.environ.get('JAVA_HOME', str(Path.home() / '.local/share/air-jdk'))))
p.add_argument('--sizes', type=int, nargs='+', default=[12, 13, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 36])
p.add_argument('--vertical-half-sizes', type=int, nargs='*', default=[13], help='Also bake half-pixel vertical placement for these sizes (stats tab uses 13)')
p.add_argument('--wrapped-bold-sizes', type=int, nargs='*', default=[16, 18], help='Also bake quarter-pixel vertical placement for these bold sizes (wrapped SellableObjectPanel and PortalPanel names use 16 and 18)')
p.add_argument('--ascii', action='store_true', help='Small test pack; default includes original font Unicode coverage')
p.add_argument('--phases', type=int, choices=[1, 20], default=20, help='Flash positions glyphs on a twentieth-pixel grid')
p.add_argument('--loose', action='store_true', help='Write individual glyph PNGs for inspection instead of the compact bundle')
p.add_argument('--reference-shadows', action='store_true', help='Also export filtered fonts to compare complete shadowed fields')
p.add_argument('--supersample-probe', action='store_true', help='Export glyphs at four times the scale for rasterizer research')
a = p.parse_args(); a.output = a.output.resolve(); a.build = a.build.resolve()
inventory = json.loads((a.build / 'generated/source_inventory.json').read_text())
root = Path(inventory['source_root'])
fonts = []
for bold in (False, True):
    with TTFont(root / ('src/com/company/ui/fonts/MyriadProBold.otf' if bold else 'src/com/company/ui/fonts/MyriadPro.otf')) as font:
        codes = sorted(code for code in font.getBestCmap() if 32 <= code < 65536 and (not a.ascii or code <= 126))
    for size in a.sizes:
        if size < 6 or size > 96:
            p.error('Font size outside supported range')
        for shadow in ((False, True) if a.reference_shadows else (False,)):
            # BitmapData.draw floors text to a quarter-pixel vertical grid.
            vertical_phases = sorted({0} | ({.5} if size in a.vertical_half_sizes else set()) | ({.25, .5, .75} if bold and size in a.wrapped_bold_sizes else set()))
            for vertical_phase in vertical_phases:
                suffix = '-yhalf' if vertical_phase == .5 else f'-y{round(vertical_phase * 20)}' if vertical_phase else ''
                fonts.append(dict(id=f'{size}-{int(bold)}-{int(shadow)}'+suffix, size=size, bold=bold, shadow=shadow, vertical_phase=vertical_phase, codes=codes, kern_codes=[code for code in codes if 32 <= code <= 126]))
a.output.mkdir(parents=True, exist_ok=True)
for name in ('manifest.json', 'fontpack.json', 'glyph-index.bin', 'font-error.log'):
    (a.output / name).unlink(missing_ok=True)
tasks = dict(output=str(a.output), source_sha256=inventory['source_sha256'], phases=a.phases, bundle=not a.loose, probe=a.supersample_probe, fonts=fonts, samples=['HP', '100/100', 'Lvl 1', 'NativeTest', 'AVATAR', 'Hello, world!', 'jazz fj ÄÖÜ éç ñ', 'Welcome to the Nexus!', '0123456789 - (75) +15', 'First line\nSecond line'])
(a.output / 'tasks.json').write_text(json.dumps(tasks))
reference = a.build / 'flash-font-reference'
build('FontExporter', root, a.sdk.resolve(), a.java_home.resolve(), reference)
subprocess.run([str(a.sdk / 'bin/adl'), str(reference / 'FontExporter-app.xml'), str(reference), '--', str(a.output / 'tasks.json')], check=True, timeout=300)
assert (a.output / 'manifest.json').exists(), 'Font exporter did not finish'
if not a.loose:
    compile_pack(a.output)
print(f'Baked {len(fonts)} original Myriad font variants into {a.output}')
