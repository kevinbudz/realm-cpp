#!/usr/bin/env python3
"""Death and fame screens: production SDL input with disposable death TCP / char-fame HTTP peers."""
import argparse
import json
import socket
import struct
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary-dir', type=Path, default=ROOT / 'build')
p.add_argument('--output', type=Path, default=ROOT / 'build/death-fame-gpu')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
utf = lambda s: struct.pack('>H', len(s.encode())) + s.encode()
frame = lambda i, b: struct.pack('>IB', len(b) + 5, i) + b

def exact(c, n):
    result = b''
    while len(result) < n:
        data = c.recv(n-len(result))
        if not data:
            raise EOFError()
        result += data
    return result

def receive(c):
    n = struct.unpack('>I', exact(c, 4))[0]
    data = exact(c, n-4)
    return data[0], data[1:]

def char_list(dead=False):
    chars = [8] if dead else [7, 8]
    return ('<Chars nextCharId="9" maxNumChars="3"><Account><AccountId>1</AccountId><Name>FameFixture</Name></Account>' + ''.join(
        f'<Char id="{i}"><ObjectType>782</ObjectType><Level>20</Level><Exp>{100-i}</Exp><CurrentFame>400</CurrentFame></Char>' for i in chars) + '</Chars>').encode()

results = {}
for scenario in ('success', 'error', 'late'):
    posts, errors, packets, loads = [], [], [], []
    dead = threading.Event()
    refreshed = threading.Event()
    fame_posts = []
    late_release = threading.Event()
    stop_observer = threading.Event()
    pending_path = a.output / f'{scenario}-refresh-pending.png'
    if scenario == 'late':
        pending_path.unlink(missing_ok=True)
    class App(BaseHTTPRequestHandler):
        def do_POST(self):
            fields = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(), keep_blank_values=True)
            posts.append((self.path, fields))
            if self.path == '/account/verify':
                reply = b'<Success/>'
            elif self.path == '/char/list':
                if dead.is_set():
                    refreshed.set()
                reply = char_list(dead.is_set())
            elif self.path == '/char/fame':
                fame_posts.append(fields)
                if fields != {'accountId':['42'], 'charId':['7']}:
                    errors.append(AssertionError(fields))
                # A dismissed request finishes after Enter. Its result must
                # be consumed before the queued character-list refresh.
                if scenario == 'late':
                    if not late_release.wait(120):
                        errors.append(AssertionError('late dismissal capture missing'))
                if scenario == 'error':
                    reply = b'<Error>Fixture fame unavailable</Error>'
                elif scenario == 'success' and len(fame_posts) == 1:
                    reply = b'<Error>Fixture temporary failure</Error>'
                elif scenario == 'success' and len(fame_posts) == 2:
                    reply = b'<Fame><Char id="999"/></Fame>'
                else:
                    reply = (ROOT / 'tests/fixtures/death-fame.xml').read_bytes()
            elif self.path == '/fame/list':
                assert fields == {'timespan':['week'], 'accountId':['1'], 'charId':['7']}, fields
                reply = b'<FameList timespan="week"/>'
            else:
                errors.append(AssertionError(self.path))
                reply = b'<Error>Unexpected route</Error>'
            self.send_response(200)
            self.send_header('Content-Length', str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)
        def log_message(self, *args):
            pass
    with ThreadingHTTPServer(('127.0.0.1', 0), App) as http, socket.socket() as listener, tempfile.TemporaryDirectory() as temp:
        http_thread = threading.Thread(target=http.serve_forever, daemon=True)
        http_thread.start()
        listener.bind(('127.0.0.1', 0));listener.listen(2);listener.settimeout(180)
        def peer():
            try:
                for connection in range(2):
                    c, _ = listener.accept()
                    with c:
                        c.settimeout(180)
                        ident, _ = receive(c);assert ident == 15
                        c.sendall(frame(20, struct.pack('>ii', 100, 100) + utf('Nexus') + utf('Death fixture') + struct.pack('>Ii??', 1, 0, True, True)))
                        ident, body = receive(c);assert ident == 21
                        cid = struct.unpack('>i', body)[0];loads.append(cid)
                        assert cid == (7 if connection == 0 else 8), loads
                        stats = bytes([28]) + utf('FameFixture') + bytes([7]) + struct.pack('>i',20) + bytes([0]) + struct.pack('>i',80) + bytes([1]) + struct.pack('>i',80)
                        status = struct.pack('>iffB', 99, 50, 50, 4) + stats
                        update = struct.pack('>hhH', 0, 1, 782) + status + struct.pack('>h', 0)
                        c.sendall(frame(1,struct.pack('>ii',99,cid)) + frame(9,update))
                        while True:
                            try:
                                ident, body = receive(c)
                            except (EOFError, ConnectionResetError):
                                break
                            packets.append(ident)
                            if connection == 0 and ident == 5:
                                # Actual Enter/text/Enter sent PLAYERTEXT.
                                assert body == utf('fixture-death')
                                time.sleep(.08)  # Open Options before authoritative DEATH.
                                dead.set()
                                death = frame(24, struct.pack('>ii',42,7) + utf('Medusa'))
                                c.sendall(death[:3]);c.sendall(death[3:] + death)
                                # Close immediately: exercises DEATH + EOF in one poll.
                                break
            except BaseException as error:
                errors.append(error)
        peer_thread = threading.Thread(target=peer, daemon=True);peer_thread.start()
        directory = Path(temp)
        account = directory/'account.json'
        account.write_text(json.dumps({'username':'FameFixture','password':'disposable-fixture','charIdUseMap':{}}))
        shots = {}
        def capture(ms, name):
            path = (a.output / f'{scenario}-{name}.png').resolve();shots[name]=path
            return f'{ms} capture {path}'
        lines = ['1000 click 400 543', '1500 click 100 135', capture(2200,'world'),
                 '2300 key Return', '2320 text fixture-death', '2340 key Return',
                 '2360 key O', capture(2600,'options'), capture(3600,'fade'),
                 '3800 click 400 540', capture(4100,'early-dismiss'), capture(6000,'ready')]
        if scenario == 'success':
            lines += [capture(19000,'score-bottom'), '19100 wheel 100', capture(19400,'score-top'),
                      '19500 down 784 340', '19520 move 784 430', '19540 up 784 430', capture(19800,'scroll-drag'),
                      '19900 resize 1280 720', capture(20300,'720'), '20400 resize 1600 1200', capture(20800,'2x'),
                      '20900 resize 3840 2160', capture(21600,'4k'), '21700 resize 800 600',
                      capture(22100,'restored'), '22120 move 400 542', capture(22160,'hover'), '22200 click 400 540']
            return_at = 23000
        elif scenario == 'error':
            lines += [capture(8500,'error'), '8600 click 400 374', capture(8900,'retry'), '12000 key Escape']
            return_at = 14000
        else:
            lines += ['6200 key Return', capture(6700,'refresh-pending')]
            return_at = 19000
        lines += [capture(return_at-800,'legends'), f'{return_at-700} key Return']
        lines += [capture(return_at,'characters'), f'{return_at+400} click 100 135',
                  capture(return_at+1600,'new-world'), f'{return_at+1800} quit']
        script = directory/'input.script';script.write_text('\n'.join(lines)+'\n')
        log = a.output / f'{scenario}.log'
        command = [str(a.binary_dir.resolve()/'realm_client'), '--server','127.0.0.1',
            '--app-port',str(http.server_port),'--port',str(listener.getsockname()[1]),'--account-file',str(account),
            '--settings-file',str(directory/'settings.json'),'--hidden','--benchmark','--frames','100000',
            '--fixture-step-ms','20','--stress-delay','2','--script',str(script),
            '--telemetry',str((a.output/f'{scenario}.jsonl').resolve())]
        def release_late():
            while not stop_observer.is_set() and not pending_path.exists():
                time.sleep(.01)
            if pending_path.exists():
                late_release.set()
        observer = threading.Thread(target=release_late, daemon=True)
        if scenario == 'late':
            observer.start()
        result = subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
        stop_observer.set()
        if scenario == 'late':
            observer.join(1)
            assert late_release.is_set(), 'HTTP remained pending until dismissal capture'
        log.write_text(result.stdout+result.stderr)
        http.shutdown();peer_thread.join(3)
        assert result.returncode == 0, result.stdout+result.stderr
        assert not errors, errors
        assert not peer_thread.is_alive(), f'second character connection missing: loads={loads}, posts={posts}, packets={packets}'
        assert dead.is_set() and refreshed.is_set() and loads == [7,8], (posts,loads)
        assert len(fame_posts) == (3 if scenario == 'success' else 1 if scenario == 'late' else 6), posts
        assert '[rmlui error]' not in result.stderr, result.stderr
        images = {name:Image.open(path).convert('RGB') for name,path in shots.items()}
        if scenario == 'success':
            assert images['720'].size == (1280,720) and images['4k'].size == (3840,2160)
            region = images['hover'].crop((300,524,500,575))
            tinted = sum(count for count,(r,g,b) in region.getcolors(region.width*region.height) if r > 230 and 180 < g < 235 and 80 < b < 155)
            assert tinted > 50, 'continue uses the source hover tint'
            assert ImageChops.difference(images['score-bottom'].crop((8,316,792,466)), images['score-top'].crop((8,316,792,466))).getbbox(), 'scroll changes score lines'
        # Reloading starts a fresh game with character 8 and no retained fame/Options screen.
        assert 8 in loads and 7 == loads[0]
        results[scenario] = {'fame_requests':len(fame_posts),'loads':loads,'captures':len(shots),'requests':[path for path,_ in posts]}
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('Death/EOF/duplicate, fame retries/errors/late dismissal, score scrolling/resize and character return/re-entry pass')
