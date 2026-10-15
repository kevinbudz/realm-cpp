#!/usr/bin/env python3
"""Check live currency and map-controlled safe-area overlays with an isolated wire peer."""
import json
import socket
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from PIL import Image, ImageChops
ROOT = Path(__file__).resolve().parents[1]
output = ROOT / 'build/tests/hud-overlay-gpu'
output.mkdir(exist_ok=True)
utf = lambda value: struct.pack('>H', len(value.encode())) + value.encode()
frame = lambda ident, body: struct.pack('>IB', len(body) + 5, ident) + body


def exact(conn, count):
    result = b''
    while len(result) < count:
        part = conn.recv(count - len(result))
        assert part, 'Disconnected before fixture completed'
        result += part
    return result


def packet(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    data = exact(conn, length - 4)
    return data[0], data[1:]


def status(changed=False):
    values = {0: 200, 1: 200, 3: 200, 4: 200, 22: 5, 25: 5, 28: 'Fixture',
              33: 987654 if changed else 250, 27: 24 if changed else 12,
              56: 'Changed Guild' if changed else 'Native Guild', 57: 40 if changed else 20,
              36: 5000 if changed else 125}
    body = struct.pack('>iffB', 99, 3., 3., len(values))
    for key, value in values.items():
        body += bytes([key]) + (utf(value) if isinstance(value, str) else struct.pack('>i', value))
    return body


for displays in (False, True):
    folder = output / str(int(displays))
    folder.mkdir(exist_ok=True)
    first, second = folder / 'first.png', folder / 'changed.png'
    first.unlink(missing_ok=True)
    second.unlink(missing_ok=True)
    errors = []
    with socket.socket() as listener, tempfile.TemporaryDirectory() as temporary:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)

        def serve():
            try:
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(10)
                    assert packet(conn)[0] == 15
                    conn.sendall(frame(20, struct.pack('>ii', 8, 8) + utf('Nexus') + utf('Nexus') +
                                       struct.pack('>Ii??', 77, 0, displays, True)))
                    assert packet(conn) == (21, struct.pack('>i', 7))
                    tiles = b''.join(struct.pack('>hhH', x, y, 0x48) for y in range(8) for x in range(8))
                    update = struct.pack('>h', 64) + tiles + struct.pack('>hH', 1, 0x030e) + status() + struct.pack('>h', 0)
                    conn.sendall(frame(1, struct.pack('>ii', 99, 7)) + frame(9, update))
                    deadline = time.monotonic() + 8
                    while not first.exists() and time.monotonic() < deadline:
                        time.sleep(.01)
                    assert first.exists(), 'First gameplay frame was not captured'
                    conn.sendall(frame(11, struct.pack('>h', 1) + status(True) + bytes([0])))
                    while conn.recv(65536):
                        pass
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        temporary = Path(temporary)
        script = temporary / 'input.script'
        script.write_text(f'600 capture {first}\n1100 capture {second}\n1200 quit\n')
        result = subprocess.run([str(ROOT / 'build/realm_client'), '--connect', '127.0.0.1', '--port', str(listener.getsockname()[1]),
                                 '--username', 'Fixture', '--character', '7', '--hidden', '--benchmark', '--frames', '100000',
                                 '--script', str(script)], cwd=ROOT, capture_output=True, text=True, timeout=20)
        worker.join(2)
        (folder / 'client.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr[-1000:]
        assert not errors, errors
        before, after = Image.open(first).convert('RGB'), Image.open(second).convert('RGB')
        area = (6, 4, 280, 45)
        bright = any(min(rgb) > 220 for rgb in before.crop(area).getdata())
        assert bright == displays, 'map showDisplays flag ignored'
        assert ImageChops.difference(before.crop((350, 0, 594, 45)), after.crop((350, 0, 594, 45))).getbbox(), 'currency stat changes did not appear'
        if displays:
            assert ImageChops.difference(before.crop(area), after.crop(area)).getbbox(), 'guild/star stat changes did not appear'
        print(f'showDisplays={displays}: live currency and safe-area routing passed')
