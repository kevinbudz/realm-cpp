#!/usr/bin/env python3
"""Pixel and input checks for the Options screen through the real client.

Opens the screen from the HUD button and the Options key, rebinds Move Right and drives the player with the new key,
toggles Health Bars through the screen's switch, persists settings to a file and reloads them, shows the performance panel and
resizes the window. Needs build/realm_client, Pillow and numpy.
Run by hand or through CTest (options_gpu): python3 tests/options_gpu_tests.py
"""
import json, subprocess, tempfile
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/tests/options-gpu'
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

# Window points at 800x600 (dp). The screen has no frame: the sidebar (search, categories, Reset to Defaults, then the
# Deca Done button on the footer; Back to Home is hidden) and the open category's rows, 40 apart from y 111. Key boxes are centered at x 555, switches at
# x 521.
DONE = '109 564'
KEYBINDS, GAMEPLAY = '70 77', '75 269'   # Gameplay sits below the open Keybinds category's six section links
key_row = lambda index: f'555 {111 + index * 40}'
MOVE_RIGHT, ROTATE_LEFT = key_row(3), '555 321'
ALLOW_ROTATION, HP_BARS = '521 111', '521 281'   # Gameplay, scrolled to: Camera row 0, Interface row 0
RESET = '80 501'

def main():
    # --- The HUD's Options button (CharacterDetailsView, shown in the Nexus), its hover color, and the screen it opens.
    s, _ = run('hud', ['300 capture base', '400 move 780 210', '600 capture hover', '700 click 780 210', '900 capture open',
                       f'1000 click {DONE}', '1200 capture closed', '1300 quit'])
    base, hover, opened, closed = s['base'], s['hover'], s['open'], s['closed']
    delta = changed(hover, base)
    check(delta[190:232, 762:800].sum() > 200, 'hovering tints the header button')
    # IconButton's TextToolTip ("Options", "Hotkey: O") opens above and left of the pointer; nothing else moves.
    tip = bbox(delta)
    check(tip and tip[0] >= 540 and tip[2] >= 110 and tip[3] <= 232 and tip[1] <= 800, f'only the button and its tooltip change {tip}')
    check(not changed(closed, base).any(), 'Done closes the screen and the world is untouched')
    check(np.abs(opened - base).mean() > 20, 'the Settings screen covers the game')
    category = opened[64:90, 28:112].max(2)
    check((category > 240).sum() > 60, 'the open category is white text')
    done = opened[550:580, 60:160]
    check(np.abs(done[..., 0] - done[..., 1]).max() <= 3 and np.abs(done[..., 1] - done[..., 2]).max() <= 3 and done.max() > 150,
          'Done is the greyscale Deca button')
    check(np.abs(opened[400, 400] - base[400, 400]).max() > 20, 'the game behind the screen is tinted')
    print('HUD button, hover and Done ok')

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
    s, saved = run('rebind', ['300 key o', f'400 click {MOVE_RIGHT}', '500 capture waiting', '550 key j', '600 capture bound', f'700 click {DONE}',
                              '800 capture closed', '900 hold j', '1600 capture moved', '1700 release j', '1800 quit'])
    check(changed(s['waiting'], s['bound'])[215:248, 495:615].sum() > 20 and not changed(s['waiting'], s['bound'])[:, :210].any(), 'the clicked box shows Press a key, then the key')
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
    s, _ = run('rotate-rows', ['300 key o', f'400 move {ROTATE_LEFT}', '500 capture off', f'600 click {ROTATE_LEFT}', '700 capture still-off',
                               f'800 click {GAMEPLAY}', f'900 click {ALLOW_ROTATION}', f'1000 click {KEYBINDS}', f'1100 move {ROTATE_LEFT}',
                               '1200 capture on', f'1300 click {ROTATE_LEFT}', '1400 capture waiting', '1500 quit'])
    check(not changed(s['off'], s['still-off']).any(), 'a greyed-out Rotate key ignores clicks')
    check(changed(s['on'], s['off'])[305:375, 226:620].sum() > 50 and changed(s['waiting'], s['on']).any(),
          'turning rotation on (in Gameplay) enables the Keybinds rows')
    print('camera rotation gating ok')

    # --- Health Bars through the Gameplay tab, saved and reloaded.
    s, saved = run('hpbars', ['300 capture base', '400 key o', f'500 click {GAMEPLAY}', '600 capture gameplay', f'700 click {HP_BARS}', f'800 click {DONE}',
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

    # --- Changes in two categories persist together; a corrupt file falls back to the defaults.
    s, saved = run('two-categories', ['300 key o', f'400 click {MOVE_RIGHT}', '500 key j', f'600 click {GAMEPLAY}', f'700 click {HP_BARS}', f'800 click {DONE}', '1100 quit'])
    check(saved['values']['hpBars'] == 0 and saved['values']['moveRight'] == 74, f'changes in two categories both persist ({saved["values"]["hpBars"]}, {saved["values"]["moveRight"]})')
    (OUT / 'corrupt.settings.json').write_text('{ definitely not json')
    broken, _ = run('corrupt', ['300 capture base', '400 quit'], settings=None)
    check(not changed(broken['base'], s0['base']).any(), 'defaults without a settings file')
    print('two-category changes and defaults ok')

    # --- Reset to Defaults resets every category (not only the open one), on a second click: the first only arms it, and leaving disarms it.
    change = ['300 key o', f'400 click {MOVE_RIGHT}', '500 key j', f'600 click {GAMEPLAY}', f'700 click {HP_BARS}']
    s, saved = run('reset-armed', change + [f'800 click {RESET}', '900 move 400 300', f'1000 click {RESET}', f'1100 click {DONE}', '1300 quit'])
    check(saved['values']['hpBars'] == 0 and saved['values']['moveRight'] == 74, 'one click (after leaving) only arms Reset')
    s, saved = run('reset', change + [f'800 click {RESET}', f'900 click {RESET}', f'1000 click {DONE}', '1200 quit'])
    check(saved['values']['hpBars'] == 1 and saved['values']['moveRight'] == 68, f'a second click resets every category ({saved["values"]["hpBars"]}, {saved["values"]["moveRight"]})')
    print('reset to defaults ok')

    # --- Performance panel on its key, in the top-left of the game view.
    s, _ = run('perf', ['300 capture base', '400 key p', '1300 capture panel', '1400 quit'], settings=dict(values=dict(togglePerformanceStats=80)))
    panel = bbox(changed(s['panel'], s['base']))
    # Five rows since the frame-time tails were added: FPS, MB, avg, p90 and p99.
    check(panel and panel[0] <= 12 and panel[2] <= 12 and 60 <= panel[1] <= 400 and panel[3] <= 100, f'performance stats draw top-left: {panel}')
    # --- Resizing with the screen open keeps it usable.
    plain, _ = run('resize-plain', ['300 resize 1280 720', '800 capture base', '900 quit'])
    s, _ = run('resize', ['300 key o', '400 resize 1280 720', '800 capture open', '900 click 170 677', '1100 capture closed', '1200 quit'])
    check(s['open'].shape == (720, 1280, 3) and np.abs(s['open'] - plain['base']).mean() > 20, 'the screen fills the resized window')
    check(not changed(s['closed'], plain['base']).any(), 'Done works after a resize')
    print('performance panel and resize ok')

if __name__ == '__main__':
    main()
    print('Options GPU checks passed')
