#!/usr/bin/env python3
"""Account and character-selection screen checks against disposable local HTTP peers."""
import argparse
import json
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary-dir', type=Path, default=ROOT / 'build/tests')
p.add_argument('--cases', default='password,rejected,logout,slot,poor,hover,scroll,news')
a = p.parse_args()
binary_dir = a.binary_dir.resolve()
output = ROOT / 'build/tests/startup-parity'
output.mkdir(parents=True, exist_ok=True)


def run(case):
    folder = output / case
    folder.mkdir(exist_ok=True)
    posts = []
    password = 'fixture-password'
    balance = 100 if case == 'poor' else 5000
    slots = 10 if case == 'scroll' else 2

    class Peer(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            nonlocal password, balance, slots
            fields = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(), keep_blank_values=True)
            posts.append((self.path, fields))
            if self.path == '/account/verify':
                reply = '<Success/>'
            elif self.path == '/char/list':
                reply = (f'<Chars nextCharId="2" maxNumChars="{slots}"><Account><AccountId>42</AccountId><Name>Fixture</Name>'
                         f'<Stats><Credits>12345</Credits><Fame>{balance}</Fame><ClassStats objectType="782"><BestFame>150</BestFame>'
                         '<BestLevel>20</BestLevel></ClassStats></Stats></Account><Char id="1"><ObjectType>782</ObjectType><Level>20</Level>'
                         '<CurrentFame>200</CurrentFame><Exp>100</Exp><MaxHitPoints>670</MaxHitPoints><HitPoints>500</HitPoints>'
                         '<MaxMagicPoints>385</MaxMagicPoints><MagicPoints>200</MagicPoints><Equipment>2816,2588,-1,-1</Equipment>'
                         '<ItemDatas>-1,-1,-1,-1</ItemDatas></Char></Chars>')
                if case == 'news':
                    reply = reply.replace('</Chars>', '<News><Item><Icon>fame</Icon><Title>Hall of Fame</Title>'
                                          '<TagLine>Your fallen hero</TagLine><Link>fame:7</Link><Date>1700000000</Date></Item>'
                                          '<Item><Icon>oryx</Icon><Title>Realm news</Title><TagLine>Open the news</TagLine>'
                                          '<Link>https://example.com/realm-news</Link><Date>1700000000</Date></Item></News></Chars>')
                if case == 'scroll':
                    character = reply[reply.index('<Char id='):reply.index('</Char>') + 7]
                    reply = reply.replace(character, ''.join(character.replace('id="1"', f'id="{i}"') for i in range(1, 9)))
            elif self.path == '/char/fame':
                assert fields == {'accountId': ['42'], 'charId': ['7']}, fields
                reply = (ROOT / 'tests/fixtures/death-fame.xml').read_text()
            elif self.path == '/account/changePassword':
                assert fields == {'username': ['Fixture'], 'password': ['fixture-password'], 'newPassword': ['updated-password']}, fields
                if case == 'rejected':
                    reply = '<Error>Fixture password rejected</Error>'
                else:
                    password = fields['newPassword'][0]
                    reply = '<Success/>'
            elif self.path == '/account/purchaseCharSlot':
                assert fields == {'username': ['Fixture'], 'password': [password]}, fields
                slots += 1
                balance -= 2000
                reply = '<Success/>'
            else:
                raise AssertionError(self.path)
            data = reply.encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Peer)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        account = temporary / 'account.json'
        account.write_text(json.dumps({'username': 'Fixture', 'password': password}))
        steps = []

        def add(t, action):
            steps.append(f'{t} {action}')

        def shot(t, name):
            add(t, f'capture {folder / (name + ".png")}')

        if case in ('password', 'rejected', 'logout'):
            add(1000, 'click 310 546')
            add(1100, 'move 600 300')
            shot(1300, 'details')
            if case == 'logout':
                add(1400, 'click 330 333')
                shot(1700, 'login')
                add(1800, 'quit')
            else:
                add(1400, 'click 355 314')
                shot(1600, 'change')
                # Invalid form must stay open and send no request.
                add(1700, 'key Return')
                shot(1900, 'invalid')
                add(2000, 'click 350 203')
                add(2100, 'text fixture-password')
                add(2200, 'key Tab')
                add(2300, 'text updated-password')
                add(2400, 'key Tab')
                add(2500, 'text updated-password')
                add(2600, 'key Return')
                add(2700, 'move 600 300')
                shot(3100, 'result')
                # Resize the result to ensure modal layout/input follows the stage.
                add(3200, 'resize 1600 1200')
                shot(3500, 'result-2x')
                add(3600, 'key Escape')
                shot(3900, 'after-escape')
                add(4000, 'quit')
        elif case == 'news':
            add(1000, 'click 400 544')
            add(1200, 'move 300 90')
            shot(1500, 'news')
            add(1600, 'move 500 138')
            shot(1900, 'news-hover')
            add(2000, 'click 500 138')
            add(2300, 'move 300 90')
            shot(2700, 'news-fame')
            add(2800, 'key Escape')
            shot(3200, 'news-back')
            add(3300, 'quit')
        elif case in ('hover', 'scroll'):
            add(1000, 'click 400 544')
            add(1200, 'move 600 300')
            shot(1500, 'characters')
            if case == 'hover':
                add(1600, 'move 200 140')
                shot(1900, 'tooltip')
                add(2000, 'move 230 160')
                shot(2200, 'tooltip-moved')
                add(2300, 'move 600 300')
                shot(2500, 'tooltip-dismissed')
                add(2600, 'resize 1600 1200')
                add(2800, 'move 400 280')
                shot(3100, 'tooltip-2x')
                add(3200, 'quit')
            else:
                add(1600, 'wheel -1')
                shot(1900, 'partial-row')
                add(2000, 'down 383 230')
                add(2100, 'move 383 390')
                add(2200, 'up 383 390')
                shot(2500, 'scrolled')
                add(2600, 'quit')
        else:
            add(1000, 'click 400 544')
            add(1300, 'move 600 300')
            shot(1500, 'characters')
            add(1600, 'click 200 267')
            add(1800, 'move 600 300')
            shot(2300, 'result')
            add(2400, 'quit')

        script = temporary / 'input.script'
        script.write_text('\n'.join(steps) + '\n')
        result = subprocess.run([str(binary_dir.parent / 'realm_client'), '--hidden',
                                 '--server', '127.0.0.1', '--app-port', str(server.server_port),
                                 '--account-file', str(account), '--settings-file', str(temporary / 'settings.json'),
                                 '--script', str(script)], cwd=ROOT, capture_output=True, text=True, timeout=30)
        (folder / 'client.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (case, result.stderr[-1000:])
        stored = json.loads(account.read_text())
        changes = [fields for path, fields in posts if path == '/account/changePassword']
        purchases = [fields for path, fields in posts if path == '/account/purchaseCharSlot']
        if case in ('password', 'rejected'):
            assert len(changes) == 1, (case, posts)
            assert stored['password'] == ('updated-password' if case == 'password' else 'fixture-password'), stored
            details, changed = Image.open(folder / 'details.png').convert("RGB"), Image.open(folder / 'change.png').convert("RGB")
            assert ImageChops.difference(details.crop((250, 180, 550, 400)), changed.crop((250, 180, 550, 400))).getbbox()
        elif case == 'logout':
            assert stored['username'] == '' and stored['password'] == '', stored
            assert not changes and not purchases
        elif case == 'slot':
            assert len(purchases) == 1 and slots == 3 and balance == 3000, posts
            assert len([path for path, _ in posts if path == '/char/list']) == 2, posts
        elif case == 'news':
            assert [f for path, f in posts if path == '/char/fame'] == [{'accountId': ['42'], 'charId': ['7']}], posts
            assert not any(path == '/fame/list' for path, _ in posts), 'news fame back opened legends'
            normal = Image.open(folder / 'news.png').convert('RGB')
            hover = Image.open(folder / 'news-hover.png').convert('RGB')
            assert ImageChops.difference(normal.crop((461, 116, 788, 164)), hover.crop((461, 116, 788, 164))).getbbox(), 'news hover color missing'
            assert any(r > 120 and g > 80 for r, g, b in normal.crop((400, 112, 460, 170)).get_flattened_data()), 'news icon missing'
            restored = Image.open(folder / 'news-back.png').convert('RGB')
            assert any(r > 180 and 70 < g < 190 and b < 60 for r, g, b in restored.crop((400, 112, 460, 170)).get_flattened_data()), 'news back did not restore character screen'
        elif case == 'hover':
            assert not changes and not purchases
            before = Image.open(folder / 'characters.png').convert('RGB')
            tip = Image.open(folder / 'tooltip.png').convert('RGB')
            assert tip.getpixel((388, 175)) == (54, 54, 54), 'saved-character tooltip frame missing'
            assert ImageChops.difference(before.crop((212, 155, 460, 440)), tip.crop((212, 155, 460, 440))).getbbox()
            dismissed = Image.open(folder / 'tooltip-dismissed.png').convert('RGB')
            assert dismissed.getpixel((388, 175)) != (54, 54, 54), 'tooltip remains after leaving its row'
        elif case == 'scroll':
            assert not purchases
            before = Image.open(folder / 'characters.png').convert('RGB')
            partial = Image.open(folder / 'partial-row.png').convert('RGB')
            # (300, 130) is the first row's background before and after the wheel scroll. The former probe at (12, 118)
            # fell in the glow of the class icon, which now matches CurrentCharacterRect's 52 pixel frame.
            assert partial.getpixel((300, 130)) == before.getpixel((300, 130)) == (92, 92, 92), \
                'partially visible first row vanished'
            assert ImageChops.difference(before.crop((10, 112, 391, 512)), partial.crop((10, 112, 391, 512))).getbbox(), 'wheel did not scroll'
        else:
            assert not purchases, posts
            assert slots == 2 and balance == 100
        # Currency is visible on character selection, with its source fame/coin colors.
        if case in ('slot', 'poor'):
            crop = Image.open(folder / 'characters.png').convert('RGB').crop((610, 23, 797, 53))
            assert any(r > 170 and g > 100 and b < 90 for r, g, b in crop.get_flattened_data()), 'gold coin missing'
        print(f'{case}: account state, HTTP actions and captures passed')
    server.shutdown()
    server.server_close()


for case in a.cases.split(','):
    run(case)
