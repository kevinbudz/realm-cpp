#!/usr/bin/env python3
"""Exercise real SDL window resizing, retained UI uploads and menu input.

Requires built clients, the original font/HUD packs, Pillow and an OpenGL display.
Uses an offline map and a local HTTP fixture; no live account or game server.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import subprocess
import tempfile
import threading
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'build/ui-resize-reference'
OUTPUT.mkdir(parents=True, exist_ok=True)

def run(binary, lines, extra=()):
    with tempfile.TemporaryDirectory() as directory:
        script = Path(directory) / 'resize.txt'
        script.write_text('\n'.join(lines) + '\n')
        result = subprocess.run([str(ROOT / 'build' / binary), '--fonts', str(ROOT / 'build/fonts-client'),
            '--hud-art', str(ROOT / 'build/hud-art'), '--hidden', '--benchmark', '--frames', '100000',
            '--script', str(script), *extra], cwd=ROOT, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout

def capture(time, name):
    return f'{time} capture {OUTPUT / (name + ".png")}'

world = [capture(0, 'base'), '200 resize 1280 720', capture(350, 'fractional'), '500 key B',
    '650 resize 1600 1200', capture(850, 'stats-2x'), '1000 key B', '1150 move 1268 648',
    capture(1350, 'tooltip-2x'), '1550 resize 3840 2160', '1750 move 3242 1166',
    capture(1950, 'tooltip-4k'), '2150 resize 800 600', capture(2350, 'restored'), '2500 quit']
run('realm_viewer', world, ['--map', str(ROOT / '../realm-server/Resources/Worlds/Nexus/0.jm')])
for name, size in [('base', (800, 600)), ('fractional', (1280, 720)), ('stats-2x', (1600, 1200)),
                   ('tooltip-2x', (1600, 1200)), ('tooltip-4k', (3840, 2160)), ('restored', (800, 600))]:
    assert Image.open(OUTPUT / (name + '.png')).size == size, name
base = Image.open(OUTPUT / 'base.png').convert('RGB')
restored = Image.open(OUTPUT / 'restored.png').convert('RGB')
assert ImageChops.difference(base.crop((600, 190, 800, 300)), restored.crop((600, 190, 800, 300))).getbbox() is None, 'Base HUD restores exactly'
large = Image.open(OUTPUT / 'tooltip-2x.png').convert('RGB')
old = base.crop((600, 190, 800, 300)).resize((400, 220), Image.Resampling.NEAREST)
assert ImageChops.difference(old, large.crop((1200, 380, 1600, 600))).getbbox(), 'Large HUD is freshly rendered'

class App(BaseHTTPRequestHandler):
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length', 0)))
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'<Chars nextCharId="1" maxNumChars="1"><Account><AccountId>1</AccountId><Name>Guest</Name></Account></Chars>')
    def log_message(self, *args):
        pass

with tempfile.TemporaryDirectory() as directory:
    server = ThreadingHTTPServer(('127.0.0.1', 0), App)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        pulse = subprocess.run([str(ROOT / 'build/realm_startup_resize_tests'), str(ROOT / 'build/fonts-client'),
            str(ROOT / 'build/hud-art'), str(server.server_port), str(OUTPUT)], cwd=ROOT,
            capture_output=True, text=True, timeout=20)
        assert pulse.returncode == 0, pulse.stdout + pulse.stderr
        print(pulse.stdout.strip())
        menus = [capture(250, 'title-base'), '450 resize 1280 720', capture(650, 'title-fractional'),
            '850 click 527 653', capture(1050, 'login-fractional'), '1250 resize 1600 1200',
            capture(1450, 'login-2x'), '1650 text Resize', capture(1850, 'login-typed'),
            '2050 key Escape', '2250 resize 800 600', capture(2450, 'title-restored'), '2650 quit']
        run('realm_client', menus, ['--server', 'localhost', '--app-port', str(server.server_port),
            '--account-file', str(Path(directory) / 'account.json')])
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
login = Image.open(OUTPUT / 'login-2x.png').convert('RGB')
typed = Image.open(OUTPUT / 'login-typed.png').convert('RGB')
assert login.getpixel((800, 600)) == (54, 54, 54), 'Scaled account button opens centered dialog'
assert ImageChops.difference(login.crop((500, 350, 1100, 800)), typed.crop((500, 350, 1100, 800))).getbbox(), 'Resized text field accepts input'
print('Real window resize, 4K HUD/tooltips, restored base pixels and resized menu/dialog input passed')
