#!/usr/bin/env python3
"""Production RmlUi startup and gameplay options modal: real SDL input, disposable HTTP/accounts and captures.

Both production views run the same fixture. Network assertions check validation,
retry, edited field values and cancellation. No real credentials or live services
are used.
"""
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
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary-dir', type=Path, default=ROOT / 'build')
parser.add_argument('--output', type=Path, default=ROOT / 'build/ui-bundle')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

characters = ''.join(f'<Char id="{i}"><ObjectType>0x0300</ObjectType><Level>20</Level><Exp>100</Exp><CurrentFame>{i}</CurrentFame><Equipment>-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1</Equipment></Char>' for i in range(1, 9))
CHAR_LIST = ('<Chars nextCharId="9" maxNumChars="10"><Account><AccountId>1</AccountId><Name>Fixture</Name><Credits>2000</Credits></Account>' + characters + '</Chars>').encode()

def run(binary, lines, extra, directory):
    script = directory / f'{binary}.script'
    paths = {}
    mapped = []
    for line in lines:
        parts = line.split(' ', 2)
        if parts[1] == 'capture':
            path = args.output / f'{binary}-{parts[2]}.png'
            paths[parts[2]] = path
            line = f'{parts[0]} capture {path.resolve()}'
        mapped.append(line)
    script.write_text('\n'.join(mapped) + '\n')
    telemetry = args.output / f'{binary}.jsonl'
    result = subprocess.run([str(args.binary_dir.resolve() / binary),
        '--hidden', '--benchmark', '--frames', '100000', '--fixture-step-ms', '20', '--stress-delay', '1',
        '--script', str(script), '--telemetry', str(telemetry.resolve()), *extra], cwd=ROOT,
        capture_output=True, text=True, timeout=90)
    (args.output / f'{binary}.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    assert '[rmlui error]' not in result.stderr and '[rmlui warning]' not in result.stderr, result.stderr
    return {name: Image.open(path).convert('RGB') for name, path in paths.items()}

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    requests = []
    class App(BaseHTTPRequestHandler):
        def do_POST(self):
            fields = parse_qs(self.rfile.read(int(self.headers.get('Content-Length', 0))).decode(), keep_blank_values=True)
            requests.append((self.path, fields))
            # Exercise GetCharList retry before the title, then account failure.
            response = (b'<Error>Fixture temporary failure</Error>' if len(requests) == 1 else CHAR_LIST) if self.path == '/char/list' else b'<Error>Fixture rejected</Error>'
            self.send_response(200)
            self.end_headers()
            self.wfile.write(response)
        def log_message(self, *unused):
            pass
    server = ThreadingHTTPServer(('127.0.0.1', 0), App)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        startup = [
            '80 capture loading', '1800 capture title', '1900 click 280 543',
            '2000 capture login', '2100 key Return', '2200 capture validation',
            '2300 text Abc', '2400 key Left', '2500 text Z',
            '2600 key Tab', '2700 text FixturePass9', '2800 key Return',
            '3100 capture rejected', '3200 key Escape', '3300 click 400 543',
            '3500 capture characters', '3600 wheel -2', '3700 capture scrolled',
            '3800 resize 1280 720', '4000 capture characters-720',
            '4100 resize 1600 1200', '4300 capture characters-2x',
            '4400 resize 800 600', '4500 click 490 542', '4700 capture classes',
            '4800 click 100 140', '5100 capture skins', '5200 quit']
        # Switch forms after a server error, reject a mismatched confirmation locally, then send a valid
        # registration to the disposable peer. Subsequent character/skin actions wait for these cases.
        startup = [(f'{int(line.split(" ", 1)[0]) + 1500} {line.split(" ", 1)[1]}'
                    if int(line.split(' ', 1)[0]) >= 3200 else line) for line in startup]
        startup += ['3200 click 300 372', '3300 capture registration', '3400 text RegFixture',
            '3420 key Tab', '3440 text FixturePass9', '3460 key Tab', '3480 text Mismatch',
            '3500 key Return', '3540 capture mismatch']
        startup += [f'{3600 + i * 20} key Backspace' for i in range(8)]
        startup += ['3800 text FixturePass9', '3900 key Return', '4300 capture register-rejected']
        shots = run('realm_client', startup, ['--server', '127.0.0.1', '--app-port', str(server.server_port),
            '--account-file', str(directory / 'account.json')], directory)
        assert sum(path == '/char/list' for path, _ in requests) >= 2, requests
        verification = [fields for path, fields in requests if path == '/account/verify']
        assert len(verification) == 1, requests  # Empty fields never sent.
        expected = 'AbZc'
        assert verification[0]['username'] == [expected], verification  # Caret editing is RmlUi-specific.
        assert verification[0]['password'] == ['FixturePass9'], verification
        registration = [fields for path, fields in requests if path == '/account/register']
        assert len(registration) == 1, requests  # The mismatched confirmation never reaches HTTP.
        assert registration[0]['newUsername'] == ['RegFixture'] and registration[0]['newPassword'] == ['FixturePass9'], registration
        assert shots['login'].getpixel((400, 300)) == (54, 54, 54), 'centered login panel'
        assert ImageChops.difference(shots['login'], shots['validation']).getbbox(), 'local validation changes view'
        assert ImageChops.difference(shots['characters'].crop((0, 110, 399, 510)), shots['scrolled'].crop((0, 110, 399, 510))).getbbox(), 'character wheel moves rows'
        assert shots['characters-720'].size == (1280, 720) and shots['characters-2x'].size == (1600, 1200)
        results = {'requests': [path for path, _ in requests], 'startup_captures': len(shots)}
    finally:
        server.shutdown()
        server.server_close()
        worker.join()

    gameplay = ['200 capture hud', '300 key O', '500 capture options', '600 resize 1280 720',
        '800 capture options-720', '900 click 640 648', '1100 capture closed',
        '1200 resize 1600 1200', '1400 move 1268 648', '1600 capture tooltip-2x',
        '1700 resize 800 600', '1900 capture restored', '2000 quit']
    shots = run('realm_client', gameplay, ['--map', str(ROOT.parent / 'realm-server/Resources/Worlds/Nexus/0.jm'),
        '--effect-time', '0', '--scene-time', '0'], directory)
    assert shots['options-720'].size == (1280, 720)
    assert ImageChops.difference(shots['hud'], shots['options']).getbbox(), 'options modal appears'
    assert ImageChops.difference(shots['hud'].crop((600, 190, 800, 300)), shots['restored'].crop((600, 190, 800, 300))).getbbox() is None, 'HUD restored after modal/resize'
    results['gameplay_captures'] = len(shots)

(args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print('Production RmlUi startup retry, validation, editing, wheel, resize and gameplay modal checks passed')
