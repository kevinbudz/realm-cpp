#!/usr/bin/env python3
"""Exercise the editor through Startup/RmlUi, save/load a JM, and return from an actual map-test handshake."""
import argparse
import base64
import json
import select
import socket
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary-dir', type=Path, default=ROOT / 'build/tests')
parser.add_argument('--output', type=Path, default=ROOT / 'build/tests/editor-gpu')
args = parser.parse_args()
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
utf = lambda text: struct.pack('>H', len(text.encode())) + text.encode()
frame = lambda ident, body: struct.pack('>IB', len(body) + 5, ident) + body


def exact(conn, count):
    result = b''
    while len(result) < count:
        part = conn.recv(count - len(result))
        assert part, 'client closed the editor test connection early'
        result += part
    return result


def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    body = exact(conn, length - 4)
    return body[0], body[1:]


def read_utf(body, at):
    length = struct.unpack_from('>H', body, at)[0]
    at += 2
    return body[at:at + length].decode(), at + length


errors, launches = [], []


class HttpPeer(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        if self.path == '/account/verify':
            reply = '<Success/>'
        elif self.path == '/char/list':
            reply = ('<Chars nextCharId="8" maxNumChars="2"><Account><AccountId>42</AccountId>'
                     '<Name>EditorFixture</Name><Stats/></Account><Char id="7"><ObjectType>782</ObjectType>'
                     '<Level>20</Level><Equipment>-1,-1,-1,-1</Equipment></Char></Chars>')
        else:
            errors.append(AssertionError(f'unexpected editor HTTP endpoint {self.path}'))
            reply = '<Error>Unexpected endpoint</Error>'
        data = reply.encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


with tempfile.TemporaryDirectory(prefix='realm-editor-gpu-') as temporary, socket.socket() as listener:
    temp = Path(temporary)
    saved = temp / 'edited.jm'
    account = temp / 'account.json'
    account.write_text(json.dumps({'username': 'EditorFixture', 'password': 'fixture-password'}))
    http = ThreadingHTTPServer(('127.0.0.1', 0), HttpPeer)
    http_thread = threading.Thread(target=http.serve_forever, daemon=True)
    http_thread.start()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(20)

    def game_peer():
        try:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(15)
                ident, body = receive(conn)
                assert ident == 15, f'expected HELLO, got {ident}'
                version, at = read_utf(body, 0)
                game_id = struct.unpack_from('>i', body, at)[0]
                at += 4
                username, at = read_utf(body, at)
                password, at = read_utf(body, at)
                map_length = struct.unpack_from('>I', body, at)[0]
                at += 4
                map_json = body[at:at + map_length].decode()
                at += map_length
                assert at == len(body) and game_id == -4 and username == 'EditorFixture'
                assert password == 'fixture-password' and version
                document = json.loads(map_json)
                assert document['width'] == 2 and document['height'] == 1, document
                assert len(zlib.decompress(base64.b64decode(document['data']))) == 4
                launches.append(document)
                conn.sendall(frame(20, struct.pack('>ii', 10, 10) + utf('Editor Test') + utf('Editor Test') +
                                   struct.pack('>Ii??', 1, 0, True, True)))
                ident, body = receive(conn)
                assert ident == 21 and body == struct.pack('>i', 7), 'map test must LOAD the selected existing character'
                status = struct.pack('>iffB', 99, 5.5, 5.5, 0)
                conn.sendall(frame(1, struct.pack('>ii', 99, 7)) +
                             frame(9, struct.pack('>hhH', 0, 1, 782) + status + struct.pack('>h', 0)))
                deadline = time.monotonic() + 10
                while not (OUT / 'playing.png').exists():
                    assert time.monotonic() < deadline, 'map test did not reach its playing capture'
                    time.sleep(.01)
                conn.sendall(frame(19, struct.pack('>i', -1)))
                # RECONNECT must close the test, rather than attempting another game connection.
                while True:
                    ready, _, _ = select.select([conn], [], [], 3)
                    assert ready, 'map test did not close after RECONNECT'
                    if not conn.recv(4096):
                        break
        except BaseException as error:
            errors.append(error)

    game_thread = threading.Thread(target=game_peer, daemon=True)
    game_thread.start()
    for path in OUT.glob('*.png'):
        path.unlink()
    actions = []

    def add(ms, action):
        actions.append(f'{ms} {action}')

    def shot(ms, name):
        add(ms, f'capture {OUT / (name + ".png")}')

    add(1200, 'click 40 546')
    add(1400, 'move 300 60')
    shot(1800, 'empty')
    add(2000, 'down 350 300')
    add(2100, 'move 366 300')
    add(2200, 'up 366 300')
    add(2250, 'move 300 60')
    shot(2500, 'painted')
    add(2600, 'key U')
    shot(2800, 'undone')
    add(2900, 'key R')
    shot(3100, 'redone')
    add(3200, 'key S')
    for index in range(8):
        add(3300 + index * 15, 'key Backspace')
    add(3500, f'text {saved}')
    add(3700, 'key Return')
    shot(4000, 'saved')
    add(4100, 'key C')
    shot(4400, 'cleared')
    add(4500, 'key L')
    add(4700, 'key Return')
    shot(5000, 'loaded')
    add(5300, 'key T')
    shot(6500, 'playing')
    shot(8000, 'returned')
    add(8200, 'resize 1600 1200')
    shot(8800, 'resized')
    add(9000, 'quit')
    script = temp / 'actions.txt'
    script.write_text('\n'.join(actions) + '\n')
    result = subprocess.run([str(args.binary_dir.resolve().parent / 'realm_client'), '--hidden', '--server', '127.0.0.1',
                             '--app-port', str(http.server_port), '--port', str(listener.getsockname()[1]),
                             '--account-file', str(account), '--settings-file', str(temp / 'settings.json'),
                             '--audio', 'off', '--script', str(script)], cwd=ROOT, text=True,
                            capture_output=True, timeout=25)
    (OUT / 'client.log').write_text(result.stdout + result.stderr)
    game_thread.join(5)
    http.shutdown()
    http.server_close()
    assert result.returncode == 0, result.stdout + result.stderr
    assert not game_thread.is_alive(), 'editor game peer did not finish'
    if errors:
        raise errors[0]
    assert len(launches) == 1 and saved.exists(), 'editor did not save and launch exactly one map'
    assert json.loads(saved.read_text()) == launches[0], 'map test did not use the saved/loaded document'


def pixels(name):
    return Image.open(OUT / (name + '.png')).convert('RGB')


box = (325, 275, 395, 325)
assert ImageChops.difference(pixels('empty').crop(box), pixels('painted').crop(box)).getbbox(), 'painting did not change canvas'
assert ImageChops.difference(pixels('empty').crop(box), pixels('undone').crop(box)).getbbox() is None, 'undo did not clear gesture'
assert ImageChops.difference(pixels('painted').crop(box), pixels('redone').crop(box)).getbbox() is None, 'redo did not restore gesture'
assert ImageChops.difference(pixels('cleared').crop((360, 300, 440, 370)), pixels('loaded').crop((360, 300, 440, 370))).getbbox(), 'load did not restore the centered map'
assert ImageChops.difference(pixels('loaded').crop((144, 78, 656, 590)), pixels('returned').crop((144, 78, 656, 590))).getbbox() is None, 'test return lost editor document'
assert pixels('resized').size == (1600, 1200), 'editor resize was not presented'
print('Editor startup, actual artwork, paint/undo/redo, file save/load, HELLO/LOAD map test, reconnect return and resize passed')
