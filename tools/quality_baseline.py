#!/usr/bin/env python3
"""Q00 component baseline runner. Never interprets extra quads as combat capacity.

Uses existing viewer flags, keeps capture runs separate, and writes command/log
receipts. No saved account/settings are read. Run after both preset checks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/quality-Q00')
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if args.frames <= 60 or args.repeats < 1:
        parser.error('Use >60 frames and at least one repetition')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    viewer = ROOT / 'build/realm_viewer'
    nexus = ROOT.parent / 'realm-server/Resources/Worlds/Nexus/0.jm'
    for path in [viewer, nexus, ROOT / 'build/fonts-client/fontpack.json',
                 ROOT / 'build/hud-art/manifest.json']:
        if not path.exists():
            parser.error(f'Missing prerequisite: {path}')
    base = [str(viewer), '--benchmark']
    world = ['--map', str(nexus), '--fonts', str(ROOT / 'build/fonts-client'),
             '--hud-art', str(ROOT / 'build/hud-art')]
    records = []

    def run(name, options, measured):
        command = base + options
        # Kernel child high-water RSS includes startup/driver allocations.
        # It is not a GPU memory measurement or a steady-state cache budget.
        start = time.monotonic()
        with (out / (name + '.log')).open('w') as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            if hasattr(os, 'wait4'):
                while True:
                    pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                    if pid:
                        process.returncode = os.waitstatus_to_exitcode(status)
                        break
                    if time.monotonic()-start > 120:
                        process.kill()
                        _, status, usage = os.wait4(process.pid, 0)
                        process.returncode = os.waitstatus_to_exitcode(status)
                        break
                    time.sleep(.05)
                # Linux reports KiB; this baseline's host is Linux.
                memory = dict(max_rss_native_units=usage.ru_maxrss,
                              units='KiB on Linux; bytes on macOS',
                              user_seconds=usage.ru_utime, system_seconds=usage.ru_stime)
            else:
                process.wait(timeout=120)
                memory = dict(unavailable='os.wait4 unsupported')
        (out / (name + '.memory.log')).write_text(json.dumps(memory, indent=2)+'\n')
        records.append(dict(name=name, command=command, exit=process.returncode,
                            seconds=time.monotonic()-start, measured=measured,
                            log=name+'.log', memory=name+'.memory.log'))
        (out / 'runs.json').write_text(json.dumps(records, indent=2)+'\n')
        print(name, 'exit', process.returncode, flush=True)
        if process.returncode:
            raise SystemExit(process.returncode)

    # Three independent process runs with the harness's own 30-frame warmup.
    # This preliminary protocol is intentionally shorter than Q02 qualification.
    for repeat in range(1, args.repeats+1):
        for name, scene in [('nexus', world), ('extra-quads', ['--sprites', '10000'])]:
            run(f'{name}-{repeat}', scene + ['--hidden', '--frames', str(args.frames),
                '--width', '1920', '--height', '1080'], True)
    run('nexus-visible', world + ['--frames', str(args.frames), '--width', '1920', '--height', '1080'], True)
    run('nexus-visible-capture', world + ['--frames', '120', '--width', '1920', '--height', '1080',
        '--capture', str(out/'nexus-visible-capture.png')], False)
    # PNG dimensions verify actual drawable sizes in these independent runs.
    for width, height in [(800, 600), (1280, 720), (1920, 1080), (3840, 2160)]:
        name = f'nexus-capture-{width}x{height}'
        run(name, world + ['--hidden', '--frames', '1', '--scene-time', '0',
            '--effect-time', '0', '--width', str(width), '--height', str(height),
            '--capture', str(out / (name+'.png'))], False)
    # Full per-file inventory is local evidence; summarized hashes are stable
    # inputs for later comparisons. Runtime/code excludes evolving handoffs.
    fingerprints = {}
    for name, paths in [('native', [*ROOT.glob('src/**/*'), *ROOT.glob('include/**/*'),
                                   ROOT/'CMakeLists.txt', ROOT/'CMakePresets.json']),
                        ('fonts', list((ROOT/'build/fonts-client').rglob('*'))),
                        ('hud-art', list((ROOT/'build/hud-art').rglob('*'))),
                        ('native-assets', list((ROOT/'assets').rglob('*'))),
                        ('flash', list((ROOT.parent/'realm-client/src').rglob('*'))),
                        ('map', [nexus]), ('generated', list((ROOT/'build/generated').rglob('*')))]:
        entries = {str(p.relative_to(ROOT.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in sorted(paths) if p.is_file()}
        encoded = json.dumps(entries, sort_keys=True, separators=(',', ':')).encode()
        fingerprints[name] = dict(files=len(entries), sha256=hashlib.sha256(encoded).hexdigest())
        (out/(name+'-files.json')).write_bytes(encoded+b'\n')
    (out/'fingerprints.json').write_text(json.dumps(fingerprints, indent=2)+'\n')


if __name__ == '__main__':
    main()
