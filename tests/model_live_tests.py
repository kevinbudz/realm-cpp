#!/usr/bin/env python3
"""Real server objects with Flash <Model> definitions, projected by the native model path.

Runs an isolated server with a disposable database and one fixture account; the user's
saved account and database are untouched.
"""
from pathlib import Path
import argparse, subprocess, tempfile, time, xml.etree.ElementTree as ET
from live_server_tests import free_port, request
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(); p.add_argument('server_dll', type=Path)
p.add_argument('--client', type=Path, default=ROOT / 'build/tests/realm_model_session_inputs'); a = p.parse_args()
server_root = ROOT.parent / 'realm-server'; out = ROOT / 'build/tests/model-reference'; out.mkdir(parents=True, exist_ok=True)
app, game = free_port(), free_port()
classes = ET.parse(server_root / 'Resources/GameData/EmbeddedData_PlayersCXML.xml').getroot()
wizard = next(int(o.get('type'), 0) for o in classes if o.get('id') == 'Wizard')
with tempfile.TemporaryDirectory(prefix='realm-model-live-') as directory:
    work = Path(directory); settings = ET.parse(server_root / 'Settings.xml')
    settings.getroot().set('res', str(server_root / 'Resources')); settings.getroot().set('db', str(work / 'Database'))
    settings.find('Ports').text = f'{app}:{game}'; settings.write(work / 'Settings.xml')
    with (work / 'server.log').open('w+') as log:
        server = subprocess.Popen(['dotnet', str(a.server_dll.resolve())], cwd=work, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 45
            while True:
                if server.poll() is not None: log.seek(0); raise RuntimeError(log.read())
                try:
                    if request(app, '/char/list', {}).tag == 'Chars': break
                except (OSError, TimeoutError): pass
                assert time.monotonic() < deadline; time.sleep(.1)
            name, password = 'ModelCase', 'model-private-fixture'
            assert request(app, '/account/register', dict(newUsername=name, newPassword=password), '127.0.0.1').tag == 'Success'
            r = subprocess.run([str(a.client.resolve().parent / 'realm_session'), '--server', 'localhost', '--port', str(game), '--username', name,
                                '--password-stdin', '--seconds', '1', '--create', str(wizard)], input=password + '\n', text=True, capture_output=True, timeout=15)
            assert r.returncode == 0, r.stdout + r.stderr
            deadline = time.monotonic() + 5
            while request(app, '/char/list', dict(username=name, password=password)).tag != 'Chars':
                assert time.monotonic() < deadline; time.sleep(.1)
            time.sleep(.5)
            r = subprocess.run([str(a.client.resolve()), str(game), name], input=password + '\n', text=True, capture_output=True, timeout=25)
            assert r.returncode == 0, r.stdout + r.stderr
            variant = 'sanitize' if 'sanitize' in str(a.client) else 'release'
            (out / ('live-' + variant + '.log')).write_text(r.stdout)
            print(r.stdout.strip())
        except BaseException:
            log.flush(); log.seek(0); (out / 'live-failure-server.log').write_text(log.read().replace('model-private-fixture', '[redacted]')); raise
        finally:
            server.terminate()
            try: server.wait(5)
            except subprocess.TimeoutExpired: server.kill(); server.wait()
print('Isolated real-server native model projection passed')
