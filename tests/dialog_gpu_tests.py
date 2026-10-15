#!/usr/bin/env python3
"""BUYRESULT through the real SDL client's TCP, modal input and GPU path."""
from pathlib import Path
import socket
import struct
import subprocess
import threading
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build/tests/dialog-reference/gpu'
OUTPUT.mkdir(parents=True, exist_ok=True)
utf = lambda text: struct.pack('>H', len(text.encode())) + text.encode()
frame = lambda ident, body: struct.pack('>IB', len(body) + 5, ident) + body

def exact(conn, length):
    data = bytearray()
    while len(data) < length:
        part = conn.recv(length - len(data))
        assert part, 'Unexpected disconnect'
        data.extend(part)
    return bytes(data)

def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    data = exact(conn, length - 4)
    return data[0], data[1:]

errors, packets = [], []
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(10)
    def peer():
        try:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(10)
                assert receive(conn)[0] == 15
                conn.sendall(frame(20, struct.pack('>ii', 8, 8) + utf('Nexus') + utf('Dialog fixture') + struct.pack('>Ii??', 1, 0, True, True)))
                assert receive(conn)[0] == 21
                player = struct.pack('>iffB', 99, 3., 3., 2) + bytes([1]) + struct.pack('>i', 80) + bytes([0]) + struct.pack('>i', 80)
                update = struct.pack('>hhH', 0, 1, 0x030e) + player + struct.pack('>h', 0)
                # Fragment the result to exercise the real decoder as well.
                result = frame(26, struct.pack('>i', 1) + utf('Purchase Error: Insufficient Funds.'))
                conn.sendall(frame(1, struct.pack('>ii', 99, 7)) + frame(9, update) + result[:3])
                conn.sendall(result[3:])
                while True:
                    try:
                        ident, body = receive(conn)
                    except AssertionError:
                        break
                    packets.append(ident)
        except BaseException as error:
            errors.append(error)
    worker = threading.Thread(target=peer, daemon=True)
    worker.start()
    def capture(ms, name):
        return f'{ms} capture {OUTPUT / (name + ".png")}'
    script = OUTPUT / 'input.txt'
    script.write_text('\n'.join([
        capture(500, 'base'), '600 click 100 100', '650 key R', '700 key F', '750 key 1',
        '800 key Escape', '850 key Return', '900 key G', capture(1000, 'blocked'),
        '1100 resize 1280 720', '1250 move 640 390', capture(1400, 'hover-1280'),
        '1500 resize 1600 1200', capture(1700, 'resized-1600'),
        '1800 resize 3840 2160', capture(2000, 'resized-3840'),
        '2100 resize 800 600', '2200 move 100 100', capture(2400, 'restored'),
        '2500 click 400 330', capture(2700, 'closed'), '2900 quit',
    ]) + '\n')
    result = subprocess.run([str(ROOT / 'build/realm_client'), '--connect', 'localhost',
        '--port', str(listener.getsockname()[1]), '--username', 'fixture', '--character', '7',
        '--script', str(script), '--hidden', '--benchmark', '--frames', '100000'],
        cwd=ROOT, capture_output=True, text=True, timeout=20)
    worker.join(12)
    assert not worker.is_alive(), 'Wire peer did not finish'
    if errors:
        raise errors[0]
    assert result.returncode == 0, result.stdout + result.stderr
    (OUTPUT / 'run.log').write_text(result.stdout + result.stderr)

for name, size in [('base', (800, 600)), ('hover-1280', (1280, 720)),
                   ('resized-1600', (1600, 1200)), ('resized-3840', (3840, 2160))]:
    im = Image.open(OUTPUT / (name + '.png')).convert('RGB')
    assert im.size == size
    s = size[1] / 600
    x, y = (size[0] - 301*s)/2, (size[1] - 101*s)/2
    assert im.getpixel((int(x + 10*s), int(y + 50*s))) == (54, 54, 54), name

base = Image.open(OUTPUT / 'base.png').convert('RGB').crop((249, 249, 552, 352))
for name in ['blocked', 'restored']:
    im = Image.open(OUTPUT / (name + '.png')).convert('RGB').crop((249, 249, 552, 352))
    assert ImageChops.difference(base, im).getbbox() is None, name
closed = Image.open(OUTPUT / 'closed.png').convert('RGB').crop((249, 249, 552, 352))
assert ImageChops.difference(base, closed).getbbox(), 'Close removes modal'
assert not packets, f'Modal keyboard/mouse input reached gameplay: {packets}'
print('Real TCP BUYRESULT 1, SDL modal input, hover, 720p/1200p/4K resize and Close passed')
