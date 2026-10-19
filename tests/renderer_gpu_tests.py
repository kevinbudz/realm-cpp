#!/usr/bin/env python3
"""Graphics backend selection through the real client: the --renderer override, the saved Renderer
setting, and the Vulkan fallback to OpenGL.

Vulkan is not built yet (phase 1), so asking for it must fail cleanly with "Vulkan renderer not available in
this build", tear the window down, and run OpenGL. The override is for one run and is never saved. Needs
the realm_client path as the first argument (CTest passes it) and realm-server's Nexus map beside the repo.
Run by hand or through CTest (renderer_gpu): python3 tests/renderer_gpu_tests.py build/realm_client
"""
import json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT.parent / 'realm-server/Resources/Worlds/Tutorial/Nexus.jm'
REASON = 'Vulkan UI not implemented yet'

def check(ok, why):
    if not ok: raise AssertionError(why)

def run(client, args, settings_file, extra=()):
    command = [client, '--map', str(MAP), '--hidden', '--frames', '3', '--settings-file', str(settings_file), *args, *extra]
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)

def main():
    client = sys.argv[1]
    scratch = Path(tempfile.mkdtemp(prefix='renderer-gpu-'))

    # Parsing: an unknown API is rejected before any window opens.
    bad = subprocess.run([client, '--renderer', 'metal'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         timeout=60)
    check(bad.returncode != 0 and '--renderer expects gl or vulkan' in bad.stderr, f'bad value rejected: {bad.stderr!r}')

    # Fallback: --renderer vulkan starts OpenGL, reports the reason, and records it in telemetry.
    settings = scratch / 'default.json'
    telemetry = scratch / 'fallback.jsonl'
    fallback = run(client, ['--renderer', 'vulkan'], settings, ['--telemetry', str(telemetry)])
    check(fallback.returncode == 0, f'fallback run failed: {fallback.stderr[-600:]}')
    check(f'Vulkan renderer unavailable: {REASON}; starting with OpenGL' in fallback.stderr,
          f'fallback reason not reported: {fallback.stderr[-600:]}')
    check('Renderer OpenGL' in fallback.stdout, f'banner does not name OpenGL: {fallback.stdout[-400:]}')
    check('renderer_fallback' in telemetry.read_text() and REASON in telemetry.read_text(),
          'telemetry lacks the fallback reason')

    # The override is for this run only: a saved Vulkan choice is kept and runs OpenGL without a fallback
    # when the override asks for gl.
    saved = scratch / 'saved_vulkan.json'
    saved.write_text(json.dumps({'values': {'renderer': 1}}))
    gl_override = run(client, ['--renderer', 'gl'], saved)
    check(gl_override.returncode == 0 and 'Renderer OpenGL' in gl_override.stdout and REASON not in gl_override.stderr,
          f'gl override run: {gl_override.stderr[-400:]}')
    check(json.loads(saved.read_text())['values']['renderer'] == 1, 'the override was saved')

    # The saved choice applies without an override: Vulkan is requested, and falls back.
    saved_run = run(client, [], saved)
    check(saved_run.returncode == 0 and f'{REASON}; starting with OpenGL' in saved_run.stderr,
          f'saved Vulkan choice did not fall back: {saved_run.stderr[-400:]}')
    check(json.loads(saved.read_text())['values']['renderer'] == 1, 'the saved Renderer choice changed')

    # Default: OpenGL, no fallback.
    plain = run(client, [], scratch / 'plain.json')
    check(plain.returncode == 0 and 'Renderer OpenGL' in plain.stdout and 'Vulkan' not in plain.stderr,
          f'default run: {plain.stderr[-400:]}')
    print('renderer GPU tests: override, saved choice, fallback and default checks passed')

if __name__ == '__main__':
    main()
