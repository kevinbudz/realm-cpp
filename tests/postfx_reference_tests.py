#!/usr/bin/env python3
"""Real-Flash comparison for the Map post effects (task 10-lighting).

Flash side: tools/flash-reference/PostFxExporter.as applies Map.as's BlurFilter and BLIND_FILTER to a
source frame and draws the real HurtOverlay (breath ColorTransform) and GradientOverlay over it, in AIR.
Native side: build/realm_postfx_inputs draws the same frame as the world through the real Renderer with
the same ScreenEffects. Needs the AIR SDK, a built realm_viewer (for the source frame) and Pillow.
Not registered in CTest; run by hand: python3 tests/postfx_reference_tests.py [--reuse]
"""
import argparse, json, math, subprocess, sys
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/postfx-reference'
MAP = ROOT.parent / 'realm-server/Resources/Worlds/Tutorial/Nexus.jm'
CLIP, HEIGHT = 600, 600

def blur(t): return 20 + 10 * math.sin(t / 1000)
def breath_alpha(breath, t): return (20 - breath) / 20 * abs(math.sin(t / 300)) * 0.75

CASES = [
    ('none', dict(blur=0, blind=False, vignette=0)),
    ('blind', dict(blur=0, blind=True, vignette=0)),
    ('drunk-20', dict(blur=blur(0), blind=False, vignette=0)),
    ('drunk-30', dict(blur=blur(1571), blind=False, vignette=0)),
    ('drunk-10', dict(blur=blur(4712), blind=False, vignette=0)),
    ('drunk-blind', dict(blur=blur(1000), blind=True, vignette=0)),
    ('breath-5', dict(blur=0, blind=False, vignette=breath_alpha(5, 1500))),
    ('breath-12', dict(blur=0, blind=False, vignette=breath_alpha(12, 700))),
    ('breath-drunk', dict(blur=blur(2000), blind=False, vignette=breath_alpha(0, 500))),
]

WATER = OUT / 'water.png'
BACKDROPS = [
    dict(id='stars-0', backdrop='stars', angle=0.0, time=0),
    dict(id='stars-rot', backdrop='stars', angle=0.7, time=1234),
    dict(id='water-0', backdrop='nexus-replica', angle=0.0, time=0, source=str(WATER)),
    dict(id='water-drift', backdrop='nexus-replica', angle=0.0, time=7300, source=str(WATER)),
    dict(id='water-rot', backdrop='nexus-replica', angle=0.7, time=7300, source=str(WATER)),
]
BACKDROPS += [dict(id=f'water-real-{i}', backdrop='nexus-real', angle=0.3, time=2000, seed=100 + i) for i in range(10)]

def stats(image):
    """Mean and standard deviation of the blue channel of the full frame."""
    px = list(image.getchannel('B').getdata()); mean = sum(px) / len(px)
    return mean, math.sqrt(sum((v - mean) ** 2 for v in px) / len(px))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--reuse', action='store_true'); a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'source.png'
    if not a.reuse or not source.exists():
        subprocess.run([str(ROOT / 'build/realm_viewer'), '--map', str(MAP), '--world', '--hidden', '--frames', '3', '--capture', str(source)], check=True, stdout=subprocess.DEVNULL)
    if not WATER.exists() or not a.reuse:
        subprocess.run([str(ROOT / 'build/realm_postfx_inputs'), '--water', str(WATER)], check=True)
    cases = [dict(id=n, source=str(source), clip_width=CLIP, height=HEIGHT, sidebar=True, **p) for n, p in CASES] + BACKDROPS
    (OUT / 'input.json').write_text(json.dumps(dict(output=str(OUT / 'flash'), cases=cases)))
    if not a.reuse:
        with open(OUT / 'compile.log', 'w') as log:
            subprocess.run([sys.executable, str(ROOT / 'tools/build_flash_reference.py'), 'PostFxExporter', '--output', str(OUT / 'exporter')], check=True, stdout=log, stderr=log)
        subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), '-nodebug', str(OUT / 'exporter/PostFxExporter-app.xml'), '--', str(OUT / 'input.json')], check=True, timeout=180, cwd=ROOT)
    subprocess.run([str(ROOT / 'build/realm_postfx_inputs'), str(OUT / 'flash/states.json'), str(OUT / 'native')], check=True)
    failures = []
    states = {c['id']: c for c in json.loads((OUT / 'flash/states.json').read_text())}
    for c in cases:
        if 'backdrop' in c: continue
        f = Image.open(OUT / 'flash' / (c['id'] + '.png')).convert('RGB'); n = Image.open(OUT / 'native' / (c['id'] + '.png')).convert('RGB')
        view = (0, 0, CLIP, HEIGHT)
        d = ImageChops.difference(f.crop(view), n.crop(view)).convert('L'); px = list(d.getdata())
        mean = sum(px) / len(px); over = sum(1 for v in px if v > 12) / len(px); worst = max(px)
        # Box blur edge handling differs by ~1 level; a tighter bound applies to unblurred cases.
        limit_mean, limit_over = (3.0, 0.04) if c['blur'] > 0 else (1.0, 0.002)
        ok = mean <= limit_mean and over <= limit_over
        print(f"{c['id']:14s} mean={mean:6.3f} >12={over*100:6.3f}% max={worst:3d} {'ok' if ok else 'FAIL'}")
        if not ok: failures.append(c['id'])
        pair = Image.new('RGB', (CLIP * 2 + 8, HEIGHT), (255, 0, 255)); pair.paste(f.crop(view), (0, 0)); pair.paste(n.crop(view), (CLIP + 8, 0))
        pair.save(OUT / ('comparison-' + c['id'] + '.png'))
    noise = []
    for c in BACKDROPS:
        f = Image.open(OUT / 'flash' / (c['id'] + '.png')).convert('RGB'); n = Image.open(OUT / 'native' / (c['id'] + '.png')).convert('RGB')
        pair = Image.new('RGB', (1608, 600), (255, 0, 255)); pair.paste(f, (0, 0)); pair.paste(n, (808, 0)); pair.save(OUT / ('comparison-' + c['id'] + '.png'))
        if c['backdrop'] == 'nexus-real':
            noise.append((stats(f), stats(n))); continue
        else:
            d = ImageChops.difference(f, n).convert('L'); px = list(d.getdata()); mean = sum(px) / len(px); over = sum(1 for v in px if v > 12) / len(px)
            ok = mean <= 0.6 and over <= 0.005  # nearest-neighbour resampling differs only at texel seams
            print(f"{c['id']:14s} mean={mean:6.3f} >12={over*100:6.3f}% {'ok' if ok else 'FAIL'}")
        if not ok: failures.append(c['id'])
    # Math.random seeds the real noise, so the ten fields are compared as distributions, not pixels.
    fm, fs, nm, ns = (sum(v) / len(noise) for v in zip(*[(a[0], a[1], b[0], b[1]) for a, b in noise]))
    ok = abs(fm - nm) <= 6 and abs(fs - ns) <= 0.10 * fs
    print(f"{'water-real x10':14s} blue mean flash={fm:6.1f} native={nm:6.1f} std flash={fs:5.1f} native={ns:5.1f} {'ok' if ok else 'FAIL'}")
    if not ok: failures.append('water-real')
    assert not failures, 'post effect mismatches: ' + ', '.join(failures)

if __name__ == '__main__':
    main()
