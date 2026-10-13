#!/usr/bin/env python3
"""Pixel checks for the world-space status cues through the real client and renderer.

Every case renders the Nexus map with a fixed effect and scene clock, then compares against a baseline or the
same scene without the player (--ghost). Needs build/realm_client and Pillow
plus numpy. Run by hand or through CTest (status_gpu): python3 tests/status_gpu_tests.py
"""
import subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/status'
MAP = ROOT.parent / 'realm-server/Resources/Worlds/Tutorial/Nexus.jm'
BASE = [str(ROOT / 'build/realm_client'), '--map', str(MAP), '--hidden', '--frames', '4',
        '--effect-time', '0', '--scene-time', '0']
bit = lambda condition_id: 1 << (condition_id - 1)

def render(name, *args):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f'{name}.png'
    subprocess.run(BASE + list(args) + ['--capture', str(path)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    return np.asarray(Image.open(path).convert('RGB')).astype(float)

def check(ok, why):
    if not ok: raise AssertionError(why)

def changed(a, b, threshold=0):
    return np.abs(a - b).sum(2) > threshold

def bbox(mask):
    ys, xs = np.nonzero(mask)
    return (xs.min(), xs.max(), ys.min(), ys.max()) if len(ys) else None

def main():
    base = render('base', '--hp', '100')
    ghost = render('ghost', '--ghost')
    # The player's sprite sits around (300, 303) at 800x600; the clip region is 600 px wide.
    sprite = bbox(changed(render('probe-stasis', '--condition', str(bit(19))), base))
    check(sprite is not None, 'stasis changes the sprite')
    cx = (sprite[0] + sprite[1]) / 2; top = sprite[2]
    print('player sprite box', sprite)

    # Stasis: ColorMatrixFilter greyscale on the sprite, nothing else.
    stasis = render('stasis', '--condition', str(bit(19)))
    m = changed(stasis, base)
    spread = stasis[m].max(1) - stasis[m].min(1)
    check(m.sum() > 500 and spread.max() <= 1, f'stasis sprite is grey (spread {spread.max()})')
    check(bbox(m) == sprite, 'stasis stays on the sprite (no icon, bar or text changes)')

    # Invisible: the sprite draws at 40% alpha. The shadow stays, so the lower fractions show the sprite itself.
    invisible = render('invisible', '--condition', str(bit(12)))
    m = changed(invisible, base) & (np.abs(base - ghost).sum(2) > 150)
    num, den = (invisible - ghost)[m], (base - ghost)[m]
    ok = np.abs(den) >= 60
    fraction = num[ok] / den[ok]
    low = np.percentile(fraction, 10)
    check(ok.sum() > 1000 and .37 <= low <= .43, f'invisible sprite alpha 0.4 (p10 {low:.3f}, {ok.sum()} px)')

    # Low health: pulse 0 leaves the sprite alone; near the peak red rises and green/blue fall.
    rest = render('lowhp-0', '--hp', '10')
    peak = render('lowhp-peak', '--hp', '10', '--scene-time', '314')
    m = changed(peak, rest)
    d = (peak - rest)[m]
    check(m.sum() > 800 and d[:, 0].mean() > 25 and d[:, 1].mean() < -25 and d[:, 2].mean() < -25,
          f'low health pulse (dR {d[:, 0].mean():.1f}, dG {d[:, 1].mean():.1f}, dB {d[:, 2].mean():.1f})')

    # Condition icons: 16 px icons, centered, straddling the sprite's top edge; the stasis box gives the sprite size.
    mask = bit(3) | bit(2) | bit(13) | bit(14) | bit(16) | bit(22)
    icons = render('icons', '--condition', str(mask))
    m = changed(icons, base)
    # Six icons are 96 px wide, centered on the sprite; nothing else may change.
    box = bbox(m)
    check(m.sum() > 300, 'icons drawn')
    check(abs((box[0] + box[1]) / 2 - cx) <= 4, f'icons centered on the sprite ({box} vs {cx})')
    # The sprite quad starts a few transparent pixels above its first visible pixel (top), so the icons end just above it.
    check(box[3] <= top and box[2] >= top - 30 and 90 <= box[1] - box[0] <= 112, f'icons sit on the sprite top edge: {box}, first sprite pixel {top}')
    # Icons are in the stream before the name and bars, so they never cover the health bar below.
    single = render('icon-one', '--condition', str(bit(3)))
    m = changed(single, base); box = bbox(m)
    check(m.sum() > 100 and 12 <= box[1] - box[0] <= 20 and 12 <= box[3] - box[2] <= 20, f'one icon is about 16 px: {box}')

    # Hexed players look like a Hexable pet and Hallucinating shows random lofiChar8x8 tiles; both are stable per object.
    for name, condition_id in (('hexed', 25), ('hallucinating', 8)):
        first = render(name, '--ally', '--condition', str(bit(condition_id)))
        again = render(name + '-again', '--ally', '--condition', str(bit(condition_id)))
        plain = render(name + '-plain', '--ally')
        check(not changed(first, again).any(), f'{name} look is stable between runs')
        check(changed(first, plain)[250:350, 270:330].sum() > 500, f'{name} changes the local player sprite')
        check(changed(first, plain)[330:430, 330:410].sum() > 500, f'{name} changes the second player too')
    # Names: other players only, white text with a black outline centered under the sprite.
    unnamed = render('ally', '--ally')
    named = render('ally-named', '--ally', '--ally-name', 'Bob')
    m = changed(named, unnamed)
    box = bbox(m)
    check(m.sum() > 30 and (named[m].min(1) >= 235).sum() > 15, 'the name draws white pixels')
    check(8 <= box[3] - box[2] <= 22 and 12 <= box[1] - box[0] <= 40, f'name box {box}')
    local_name = render('local-named', '--ally-name', 'Bob')
    check(not changed(local_name, base).any(), 'the local player draws no name')

    # Breath bar: back 40x4 below the health bar, cyan fill of breath/100; nothing without the stat.
    breath = render('breath', '--breath', '50')
    m = changed(breath, base); box = bbox(m)
    check(box is not None and box[1] - box[0] >= 38 and box[3] - box[2] <= 4, f'breath bar box {box}')
    fill = np.all(np.abs(breath - np.array([0x26, 0xca, 0xff])) <= 3, axis=2)
    back = np.all(np.abs(breath - np.array([0x54, 0x54, 0x54])) <= 3, axis=2)
    inside = np.zeros(back.shape, bool); inside[box[2]:box[3] + 1, box[0]:box[1] + 1] = True
    check(abs(fill.sum() - 20 * 4) <= 8 and abs((back & inside).sum() - 20 * 4) <= 8, f'half-full bar: {fill.sum()} fill, {(back & inside).sum()} back pixels')
    empty = render('breath-empty', '--breath', '0')
    m = changed(empty, base)
    check(m.sum() > 100 and not np.all(np.abs(empty - np.array([0x26, 0xca, 0xff])) <= 3, axis=2).any(), 'empty breath draws only the back')

    # Floating text: yellow notification above the sprite; damage text red; both float, at their texts' ages.
    note = render('note', '--notify', 'Quest Complete!:FFFF00')
    m = changed(note, base)
    yellow = np.all(np.abs(note - np.array([255, 255, 0])) <= 8, axis=2) & m
    box = bbox(yellow)
    check(yellow.sum() > 300 and box[3] < top - 4, f'notification above the sprite {box} (top {top})')
    check(abs((box[0] + box[1]) / 2 - cx) <= 6, 'notification centered on the object')
    aged = render('note-aged', '--notify', 'Quest Complete!:FFFF00', '--status-age', '10000')
    box2 = bbox(np.all(np.abs(aged - np.array([255, 255, 0])) <= 8, axis=2) & changed(aged, base))
    check(box2 is not None and 18 <= box[2] - box2[2] <= 22, f'text drifts 20 px in half its lifetime ({box} -> {box2})')
    hit = render('hit', '--damage', '37')
    red = np.all(np.abs(hit - np.array([255, 0, 0])) <= 8, axis=2) & changed(hit, base)
    above = red[:top - 4]
    check(above.sum() > 60 and abs((bbox(above)[0] + bbox(above)[1]) / 2 - cx) <= 6, 'damage text is red, centered, above the sprite (hit particles stay on the sprite)')
    pierced = render('hit-effects', '--damage-effects', '3', '--status-age', '500')
    check(changed(pierced, base).sum() > 300, 'condition names draw')
    print('status gpu fixtures passed')

if __name__ == '__main__': main()
