#!/usr/bin/env python3
"""Real-Flash comparison for the status art (task 11-status-effects).

Flash side: tools/flash-reference/StatusExporter.as exports ConditionEffect's own condition icons, the
CharacterStatusText text sprite and GameObject's name bitmap in AIR. Native side: build/realm_status_inputs
draws the same art through Appearances::condition_icon and StatusArt. Needs the AIR SDK and Pillow.
Not registered in CTest; run by hand: python3 tests/status_reference_tests.py [--reuse]
"""
import argparse, json, os, subprocess, sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/status-reference'
JAVA = Path(os.environ.get('JAVA_HOME', str(Path.home() / 'Downloads/idea-IU-192.7142.36/jbr')))
TEXTS = [('damage', '-37', 0xFF0000), ('pierced', '-120', 0x9000FF), ('condition', 'Armor Broken', 0xFF0000),
         ('level', 'Level Up!', 0x00FF00), ('fame', '+250 Fame', 0xE25F00), ('notice', 'Quest Complete!', 0xFFFF00)]
NAMES = [('short', 'Bob'), ('long', 'Sir Reginald III'), ('mixed', 'xXdragonXx')]

def on_black(path):
    image = Image.open(path).convert('RGBA')
    backdrop = Image.new('RGBA', image.size, (0, 0, 0, 255))
    return Image.alpha_composite(backdrop, image).convert('RGB')

def compare(a, b):
    """Mean absolute channel difference over the common canvas, the worst pixel, and pixels off by > 48."""
    width, height = max(a.width, b.width), max(a.height, b.height)
    canvas = lambda image: Image.new('RGB', (width, height)).paste(image) or None
    x, y = Image.new('RGB', (width, height)), Image.new('RGB', (width, height))
    x.paste(a); y.paste(b)
    pa, pb = list(x.getdata()), list(y.getdata())
    total = worst = off = 0
    for p, q in zip(pa, pb):
        d = max(abs(p[i] - q[i]) for i in range(3)); worst = max(worst, d); off += d > 48
        total += sum(abs(p[i] - q[i]) for i in range(3)) / 3
    return total / len(pa), worst, off

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--reuse', action='store_true'); a = ap.parse_args()
    flash, native = OUT / 'flash', OUT / 'native'
    OUT.mkdir(parents=True, exist_ok=True)
    tasks = {'output': str(flash), 'texts': [dict(id=i, text=t, color=c) for i, t, c in TEXTS],
             'names': [dict(id=i, text=t) for i, t in NAMES], 'fonts': str(ROOT / 'build/fonts-client'),
             'flash_metrics': str(flash / 'metrics.json')}
    (OUT / 'input.json').write_text(json.dumps(tasks))
    if not a.reuse or not (flash / 'metrics.json').exists():
        if not (OUT / 'exporter/StatusExporter.swf').exists():
            subprocess.run([sys.executable, str(ROOT / 'tools/build_flash_reference.py'), 'StatusExporter', '--output', str(OUT / 'exporter'), '--java-home', str(JAVA)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), '-nodebug', str(OUT / 'exporter/StatusExporter-app.xml'), '--', str(OUT / 'input.json')], check=True, timeout=180, cwd=ROOT)
    subprocess.run([str(ROOT / 'build/realm_status_inputs'), str(OUT / 'input.json'), str(native)], check=True)
    metrics = json.loads((flash / 'metrics.json').read_text())
    worst_group = {}
    def report(kind, key):
        fa, na = on_black(flash / f'{key}.png'), on_black(native / f'{key}.png')
        mean, worst, off = compare(fa, na)
        print(f'{key:24s} {fa.size} vs {na.size} mean={mean:.2f} max={worst} off48={off}')
        worst_group.setdefault(kind, []).append(mean)
    for icon in metrics['icons']: report('icon', f"icon-{icon['id']}-{icon['index']}")
    for text in TEXTS: report('text', f'text-{text[0]}')
    for name in NAMES: report('name', f'name-{name[0]}')
    for kind, values in worst_group.items(): print(f'{kind}: mean of means {sum(values) / len(values):.2f}, worst {max(values):.2f}')

if __name__ == '__main__': main()
