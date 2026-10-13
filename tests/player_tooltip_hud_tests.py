#!/usr/bin/env python3
"""Native party hover review against an independent TCP fixture (no live account).
Check held identities, authoritative HP refresh and removal in actual app frames.
"""
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]


def utf(value):
    data = value.encode()
    return struct.pack('>H', len(data)) + data


def frame(ident, body):
    return struct.pack('>IB', len(body) + 5, ident) + body


def exact(conn, length):
    data = bytearray()
    while len(data) < length:
        part = conn.recv(length - len(data))
        assert part, 'Unexpected disconnect'
        data.extend(part)
    return bytes(data)


def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    assert 5 <= length <= 1048576
    data = exact(conn, length - 4)
    return data[0], data[1:]


def status(oid, x, y, values):
    data = struct.pack('>iffB', oid, x, y, len(values))
    for key, value in values.items():
        data += bytes([key]) + (utf(value) if isinstance(value, str) else struct.pack('>i', value))
    return data


def changed(a, b, box):
    return ImageChops.difference(a.crop(box).convert('RGB'), b.crop(box).convert('RGB')).getbbox() is not None


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--width', type=int, choices=(800, 1280))
parser.add_argument('--binary', type=Path, default=ROOT / 'build/realm_client')
parser.add_argument('--output', type=Path, help='Capture directory (default: beside the selected binary)')
args = parser.parse_args()
output = (args.output or args.binary.parent / 'player-tooltip-reference').resolve()
output.mkdir(parents=True, exist_ok=True)
for width, height in [(800, 600), (1280, 720)]:
    if args.width and width != args.width:
        continue
    errors = []
    with socket.socket() as listener, tempfile.TemporaryDirectory() as directory:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)

        def server():
            try:
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(10)
                    assert receive(conn)[0] == 15, 'HELLO'
                    conn.sendall(frame(20, struct.pack('>ii', 8, 8) + utf('Nexus') + utf('Nexus') + struct.pack('>Ii??', 77, 0, True, True)))
                    assert receive(conn) == (21, struct.pack('>i', 7)), 'LOAD'
                    tiles = b''.join(struct.pack('>hhH', x, y, 0x48) for y in range(8) for x in range(8))
                    player = {0: 200, 1: 120, 3: 100, 4: 75, 7: 20, 22: 5, 25: 5, 27: 12, 56: 'Native Guild', 57: 20}
                    objects = struct.pack('>H', 0x030e) + status(99, 3, 3, player | {28: 'Original'})
                    objects += struct.pack('>H', 0x031e) + status(2, 4, 3, player | {28: 'Tank', 8: 0x0a03})
                    objects += struct.pack('>H', 0x0310) + status(3, 5, 3, player | {28: 'Healer', 56: ''})
                    update = struct.pack('>h', 64) + tiles + struct.pack('>h', 3) + objects + struct.pack('>h', 0)
                    conn.sendall(frame(1, struct.pack('>ii', 99, 7)) + frame(9, update))
                    # Refresh sorted membership and live HP while the pointer stays on Tank.
                    time.sleep(.8)
                    tick = struct.pack('>h', 2) + status(2, 7, 7, {1: 160}) + status(3, 3.1, 3, {})
                    conn.sendall(frame(11, tick))
                    time.sleep(.9)
                    conn.sendall(frame(9, struct.pack('>hhh', 0, 0, 1) + struct.pack('>i?', 3, False)))
                    while conn.recv(4096):
                        pass
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=server, daemon=True)
        worker.start()
        scale = height / 600
        x, y = round(width - 150 * scale), round(520 * scale)
        capture = lambda name: output / f'gameplay-{width}-{name}.png'
        lines = [f'250 move {x} {y}', f'600 capture {capture("hover")}',
                 f'1400 capture {capture("updated")}', '1450 move 100 100',
                 f'1500 capture {capture("released")}', f'1550 move {x} {y}',
                 f'1650 capture {capture("reordered")}', f'2000 capture {capture("removed")}', '2200 quit']
        script = Path(directory) / 'script.txt'
        script.write_text('\n'.join(lines) + '\n')
        process = subprocess.Popen([str(args.binary), '--connect', 'localhost', '--username', 'fixture', '--character', '7',
                                 '--port', str(listener.getsockname()[1]), '--hidden', '--benchmark', '--frames', '100000',
                                 '--width', str(width), '--height', str(height), '--script', str(script)],
                                   cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        peak = [0]
        finished = threading.Event()

        def memory_monitor():
            while not finished.is_set():
                try:
                    for line in Path(f'/proc/{process.pid}/status').read_text().splitlines():
                        if line.startswith('VmHWM:'):
                            peak[0] = max(peak[0], int(line.split()[1]))
                except FileNotFoundError:
                    pass
                finished.wait(.01)

        monitor = threading.Thread(target=memory_monitor, daemon=True)
        monitor.start()
        try:
            stdout, stderr = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        finally:
            finished.set()
            monitor.join(1)
        result = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        if peak[0]:
            (output / f'gameplay-{width}-memory.txt').write_text(f'Peak resident memory sampled from Linux VmHWM: {peak[0]} KiB\n')
        worker.join(5)
        assert not worker.is_alive(), 'Wire peer did not finish'
        assert result.returncode == 0, result.stdout + result.stderr
        if errors:
            raise errors[0]
        images = {name: Image.open(capture(name)) for name in ('hover', 'updated', 'released', 'reordered', 'removed')}
        assert all(image.size == (width, height) for image in images.values())
        # The first member remains Tank through the distance reorder; live HP refreshes its tooltip.
        # Names remain outside the tooltip's right edge; the tooltip sample
        # excludes HUD overlap and the moving sprites near the world center.
        party_box = (round(width - 160 * scale), round(504 * scale), width, height)
        world_box = (round(x - 252 * scale), round(height - 360 * scale), round(width - 210 * scale), height)
        assert not changed(images['hover'], images['updated'], party_box), 'Hovered party list changed identities'
        assert changed(images['hover'], images['updated'], world_box), 'Live HP did not refresh tooltip'
        assert changed(images['updated'], images['released'], party_box), 'Leaving hover did not release membership'
        assert changed(images['reordered'], images['removed'], party_box), 'Removed player stayed in party cell'
        assert not changed(images['released'], images['removed'], world_box), 'Removed player left a stale tooltip'
        (output / f'gameplay-{width}-timings.txt').write_text(result.stdout)
        print(f'{width}x{height}: stable hover, live HP, reorder and removal passed')
