#!/usr/bin/env python3
"""Graphics backend selection through the real client: the --renderer override, the saved Renderer
setting, and the Vulkan fallback to OpenGL.

A Vulkan build starts Vulkan when asked. A Vulkan start that fails must tear the window down, report why and
run OpenGL: the test forces that with REALM_VK_DEVICE naming no device (a build without Vulkan fails with
"Vulkan renderer not available in this build" instead). The override is for one run and is never saved.
Needs the realm_client path as the first argument (CTest passes it) and realm-server's Nexus map beside the
repo.
Run by hand or through CTest (renderer_gpu): python3 tests/renderer_gpu_tests.py build/realm_client
"""
import json, os, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT.parent / 'realm-server/Resources/Worlds/Tutorial/Nexus.jm'
NO_DEVICE = 'no-such-device'

def check(ok, why):
    if not ok: raise AssertionError(why)

def run(client, args, settings_file, extra=(), env=None):
    command = [client, '--map', str(MAP), '--hidden', '--frames', '3', '--settings-file', str(settings_file), *args, *extra]
    environment = {k: v for k, v in os.environ.items() if k not in ('REALM_RENDERER', 'REALM_VK_DEVICE')}
    environment.update(env or {})
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120,
                          env=environment)

def main():
    client = sys.argv[1]
    scratch = Path(tempfile.mkdtemp(prefix='renderer-gpu-'))

    # Parsing: an unknown API is rejected before any window opens.
    bad = subprocess.run([client, '--renderer', 'metal'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         timeout=60)
    check(bad.returncode != 0 and '--renderer expects gl or vulkan' in bad.stderr, f'bad value rejected: {bad.stderr!r}')

    # A Vulkan build starts Vulkan; without Vulkan the request falls back on its own.
    settings = scratch / 'default.json'
    vulkan = run(client, ['--renderer', 'vulkan'], settings)
    check(vulkan.returncode == 0, f'vulkan run failed: {vulkan.stderr[-600:]}')
    built = 'Renderer Vulkan' in vulkan.stdout
    reason = (f'REALM_VK_DEVICE matches no Vulkan device: {NO_DEVICE}' if built
              else 'Vulkan renderer not available in this build')
    if not built:
        check(f'Vulkan renderer unavailable: {reason}' in vulkan.stderr, f'no Vulkan, no reason: {vulkan.stderr[-600:]}')
    force = {'REALM_VK_DEVICE': NO_DEVICE}

    # Fallback: a Vulkan start that fails runs OpenGL, reports the reason, and records it in telemetry.
    telemetry = scratch / 'fallback.jsonl'
    fallback = run(client, ['--renderer', 'vulkan'], settings, ['--telemetry', str(telemetry)], force)
    check(fallback.returncode == 0, f'fallback run failed: {fallback.stderr[-600:]}')
    check(f'Vulkan renderer unavailable: {reason}; starting with OpenGL' in fallback.stderr,
          f'fallback reason not reported: {fallback.stderr[-600:]}')
    check('Renderer OpenGL' in fallback.stdout, f'banner does not name OpenGL: {fallback.stdout[-400:]}')
    check('renderer_fallback' in telemetry.read_text() and reason in telemetry.read_text(),
          'telemetry lacks the fallback reason')

    # The override is for this run only: a saved Vulkan choice is kept and runs OpenGL without a fallback
    # when the override asks for gl.
    saved = scratch / 'saved_vulkan.json'
    saved.write_text(json.dumps({'values': {'renderer': 1}}))
    gl_override = run(client, ['--renderer', 'gl'], saved, env=force)
    check(gl_override.returncode == 0 and 'Renderer OpenGL' in gl_override.stdout and reason not in gl_override.stderr,
          f'gl override run: {gl_override.stderr[-400:]}')
    check(json.loads(saved.read_text())['values']['renderer'] == 1, 'the override was saved')

    # The saved choice applies without an override: Vulkan is requested (and falls back when forced to).
    if built:
        saved_run = run(client, [], saved)
        check(saved_run.returncode == 0 and 'Renderer Vulkan' in saved_run.stdout,
              f'saved Vulkan choice did not start Vulkan: {saved_run.stderr[-400:]}')
    saved_run = run(client, [], saved, env=force)
    check(saved_run.returncode == 0 and f'{reason}; starting with OpenGL' in saved_run.stderr,
          f'saved Vulkan choice did not fall back: {saved_run.stderr[-400:]}')
    check(json.loads(saved.read_text())['values']['renderer'] == 1, 'the saved Renderer choice changed')

    # Default: OpenGL, no fallback.
    plain = run(client, [], scratch / 'plain.json')
    check(plain.returncode == 0 and 'Renderer OpenGL' in plain.stdout and 'Vulkan' not in plain.stderr,
          f'default run: {plain.stderr[-400:]}')
    # REALM_RENDERER acts as --renderer (test drivers use it); --renderer wins.
    env_gl = run(client, [], saved, env={'REALM_RENDERER': 'gl'})
    check(env_gl.returncode == 0 and 'Renderer OpenGL' in env_gl.stdout, f'REALM_RENDERER=gl: {env_gl.stderr[-400:]}')
    bad_env = run(client, [], settings, env={'REALM_RENDERER': 'metal'})
    check(bad_env.returncode != 0 and 'REALM_RENDERER expects gl or vulkan' in bad_env.stderr,
          f'bad REALM_RENDERER accepted: {bad_env.stderr[-400:]}')
    print('renderer GPU tests: override, saved choice, fallback and default checks passed')

if __name__ == '__main__':
    main()
