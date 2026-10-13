#!/usr/bin/env python3
"""Pixel and input checks for the Options screen through the real client.

Opens the screen from the HUD button and the Options key, rebinds Move Right and drives the player with the new key,
toggles Health Bars through the screen, persists settings to a file and reloads them, shows the performance panel and
resizes the window. Needs build/realm_client, Pillow and numpy.
Run by hand or through CTest (options_gpu): python3 tests/options_gpu_tests.py
"""
import json, subprocess, tempfile
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/options-gpu'
MAP = ROOT.parent / 'realm-server/Resources/Worlds/Tutorial/Nexus.jm'
BASE = [str(ROOT / 'build/realm_client'), '--map', str(MAP), '--hidden', '--effect-time', '0', '--scene-time', '0',
        '--hp', '50']

def check(ok, why):
    if not ok: raise AssertionError(why)

def run(name, lines, settings=None, width=None, height=None):
    """Runs the client with a script ('MS action ...' lines whose captures are 'capture NAME'); returns images and settings."""
    OUT.mkdir(parents=True, exist_ok=True)
    file = OUT / f'{name}.settings.json'
    if settings is not None: file.write_text(json.dumps(settings))
    elif file.exists(): file.unlink()
    script = []
    for line in lines:
        ms, action, *rest = line.split(' ', 2)
        script.append(f'{ms} capture {OUT / (name + "-" + rest[0] + ".png")}' if action == 'capture' else line)
    (OUT / f'{name}.txt').write_text('\n'.join(script) + '\n')
    extra = ['--width', str(width), '--height', str(height)] if width else []
    subprocess.run(BASE + extra + ['--script', str(OUT / f'{name}.txt'), '--settings-file', str(file)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    shots = {Path(l.split(' ', 2)[2]).stem[len(name) + 1:]: np.asarray(Image.open(l.split(' ', 2)[2]).convert('RGB')).astype(float)
             for l in script if l.split(' ', 2)[1] == 'capture'}
    return shots, (json.loads(file.read_text()) if file.exists() else None)

def changed(a, b, threshold=0):
    return np.abs(a - b).sum(2) > threshold

def bbox(mask):
    ys, xs = np.nonzero(mask)
    return (xs.min(), xs.max(), ys.min(), ys.max()) if len(ys) else None

# Window points at 800x600 (design units): tabs y 78, key/choice boxes (20|415 + 40, 122 + 44 row + 16), buttons.
CONTINUE = '400 540'
box = lambda index: f'{20 + 40 + (index % 2) * 395} {122 + index // 2 * 44 + 16}'

def main():
    # --- The HUD's Options button (CharacterDetailsView, shown in the Nexus), its hover color, and the screen it opens.
    s, _ = run('hud', ['300 capture base', '400 move 780 210', '600 capture hover', '700 click 780 210', '900 capture open',
                       f'1000 click {CONTINUE}', '1200 capture closed', '1300 quit'])
    base, hover, opened, closed = s['base'], s['hover'], s['open'], s['closed']
    delta = changed(hover, base)
    check(delta[190:232, 762:800].sum() > 200, 'hovering tints the header button')
    # IconButton's TextToolTip ("Options", "Hotkey: O") opens above and left of the pointer; nothing else moves.
    tip = bbox(delta)
    check(tip and tip[0] >= 540 and tip[2] >= 110 and tip[3] <= 232 and tip[1] <= 800, f'only the button and its tooltip change {tip}')
    check(not changed(closed, base).any(), 'continue closes the screen and the world is untouched')
    check(np.abs(opened - base).mean() > 20, 'the Options screen covers the game')
    title = opened[8:50, 330:470].max(2)
    check((title > 240).sum() > 150, 'the Options title is white')
    check(np.abs(opened[570, 300] - 0x54).max() <= 2 and np.abs(opened[570, 700] - 0x54).max() <= 2, f'the menu bar is the MenuFrame gray {opened[570, 300]}')
    print('HUD button, hover and continue ok')

    # --- The Options key and Escape.
    s, _ = run('keys', ['300 capture base', '400 key o', '600 capture open', '700 key Escape', '900 capture closed', '1000 quit'])
    check(np.abs(s['open'] - s['base']).mean() > 20 and not changed(s['closed'], s['base']).any(), 'O opens and Escape closes the screen, leaving the game running')
    # A rebound Options key: the default O does nothing, the new key opens it.
    s, _ = run('rebound-open', ['300 key o', '500 capture default-key', '600 key k', '800 capture new-key', '900 quit'], settings=dict(values=dict(options=75, moveUp=0)))
    base = s['default-key']
    check(np.abs(s['new-key'] - base).mean() > 20, 'a rebound Options key opens the screen')
    print('Options key and Escape ok')

    # --- Rebinding Move Right to J through the screen.
    s0, _ = run('still', ['300 capture base', '1400 quit'])
    walk = ['300 hold d', '1000 capture moved', '1100 release d', '1200 quit']
    default, _ = run('walk-d', walk)
    moved = np.abs(default['moved'] - s0['base']).mean()
    check(moved > 3, f'D moves the player by default ({moved:.1f})')
    s, saved = run('rebind', ['300 key o', f'400 click {box(3)}', '500 capture waiting', '550 key j', '600 capture bound', f'700 click {CONTINUE}',
                              '800 capture closed', '900 hold j', '1600 capture moved', '1700 release j', '1800 quit'])
    check(changed(s['waiting'], s['bound'])[166:200, 415:495].sum() > 20 and not changed(s['waiting'], s['bound'])[:, :400].any(), 'the clicked box shows [Hit Key], then the key')
    check(saved and saved['values']['moveRight'] == 74 and saved['values']['moveLeft'] == 65 and 'cameraAngle' in saved, f'the binding is written to the settings file: {saved and saved["values"]["moveRight"]}')
    also = np.abs(s['moved'] - s['closed']).mean()
    check(also > 3, f'J now moves the player ({also:.1f})')
    # The saved file is reloaded: J works from the start and the old key no longer does.
    after_j, _ = run('reload-j', ['300 hold j', '1000 capture moved', '1100 release j', '1200 quit'], settings=saved)
    check(np.abs(after_j['moved'] - s0['base']).mean() > 3, 'a reloaded J binding moves the player')
    after_d, _ = run('reload-d', ['300 hold d', '1000 capture moved', '1100 release d', '1200 quit'], settings=saved)
    check(np.abs(after_d['moved'] - s0['base']).mean() < 1.5, 'the replaced D key no longer moves the player')
    print('rebinding, persistence and reload ok')

    # --- Allow Camera Rotation gates Q/E (off by default), and the Rotate keys are greyed out until it is on.
    spin, _ = run('spin-off', ['300 hold q', '1000 capture q', '1100 release q', '1200 quit'])
    check(np.abs(spin['q'] - s0['base']).mean() < 1.5, 'Q does not rotate with Allow Camera Rotation off')
    spin, _ = run('spin-on', ['300 hold q', '1000 capture q', '1100 release q', '1200 quit'], settings=dict(values=dict(allowRotation=1)))
    check(np.abs(spin['q'] - s0['base']).mean() > 3, 'Q rotates the camera with it on')
    s, _ = run('rotate-rows', ['300 key o', '500 capture off', f'600 click {box(6)}', '700 capture still-off', f'800 click {box(4)}', '900 capture on',
                               f'1000 click {box(6)}', '1100 capture waiting', '1200 quit'])
    check(not changed(s['off'], s['still-off']).any(), 'a greyed-out Rotate key ignores clicks')
    check(np.abs(s['on'] - s['off']).mean() > .1 and changed(s['waiting'], s['on']).any(), 'turning rotation on enables the rows')
    print('camera rotation gating ok')

    # --- Health Bars through the Gameplay tab, saved and reloaded.
    s, saved = run('hpbars', ['300 capture base', '400 key o', '500 click 152 78', '600 capture gameplay', f'700 click {box(5)}', f'800 click {CONTINUE}',
                              '1000 capture off', '1100 quit'])
    bar = bbox(changed(s['off'], s['base']))
    check(bar and 36 <= bar[1] - bar[0] <= 46 and bar[3] - bar[2] <= 9, f'Health Bars off removes exactly the 42x8 bar: {bar}')
    check(saved and saved['values']['hpBars'] == 0 and saved['values']['particles'] == 1, 'only that setting changed')
    reloaded, _ = run('hpbars-reload', ['300 capture off', '400 quit'], settings=saved)
    check(not changed(reloaded['off'], s['off']).any(), 'a saved Health Bars setting is applied at startup')
    # Draw Shadows is read from the file on the Graphics tab; no shadow quad is drawn with it off.
    shadows, _ = run('shadows-off', ['300 capture off', '400 quit'], settings=dict(values=dict(drawShadows=0)))
    sh = bbox(changed(shadows['off'], s0['base'], 20))
    check(sh is not None, 'Draw Shadows off removes the shadows')
    print('health bars, shadows and startup settings ok')

    # --- Reset to defaults only affects the visible tab; a corrupt file falls back to the defaults.
    s, saved = run('reset', ['300 key o', f'400 click {box(3)}', '500 key j', '600 click 152 78', f'700 click {box(5)}', '800 click 20 540', '900 click 50 78',
                             f'1000 click {CONTINUE}', '1100 quit'])
    check(saved['values']['hpBars'] == 1 and saved['values']['moveRight'] == 74, f'reset defaults only the Gameplay tab ({saved["values"]["hpBars"]}, {saved["values"]["moveRight"]})')
    (OUT / 'corrupt.settings.json').write_text('{ definitely not json')
    broken, _ = run('corrupt', ['300 capture base', '400 quit'], settings=None)
    check(not changed(broken['base'], s0['base']).any(), 'defaults without a settings file')
    print('reset and defaults ok')

    # --- Performance panel on its key, in the top-left of the game view.
    s, _ = run('perf', ['300 capture base', '400 key p', '1300 capture panel', '1400 quit'], settings=dict(values=dict(togglePerformanceStats=80)))
    panel = bbox(changed(s['panel'], s['base']))
    # Five rows since the frame-time tails were added: FPS, MB, avg, p90 and p99.
    check(panel and panel[0] <= 12 and panel[2] <= 12 and 60 <= panel[1] <= 400 and panel[3] <= 100, f'performance stats draw top-left: {panel}')
    # --- Resizing with the screen open keeps it usable.
    plain, _ = run('resize-plain', ['300 resize 1280 720', '800 capture base', '900 quit'])
    s, _ = run('resize', ['300 key o', '400 resize 1280 720', '800 capture open', '900 click 640 648', '1100 capture closed', '1200 quit'])
    check(s['open'].shape == (720, 1280, 3) and np.abs(s['open'] - plain['base']).mean() > 20, 'the screen fills the resized window')
    check(not changed(s['closed'], plain['base']).any(), 'continue works after a resize')
    print('performance panel and resize ok')

if __name__ == '__main__':
    main()
    print('Options GPU checks passed')
