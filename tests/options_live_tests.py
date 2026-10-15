#!/usr/bin/env python3
"""Real server: the Options bindings drive the real client's packets. Uses only a disposable SQLite account.

Runs build/realm_client against an isolated server through a packet-counting proxy: a rebound inventory hot key sends
USEITEM only on its new key, Autofire Toggle sends PLAYERSHOOT only on its configured key, and the Options screen
consumes keys while open. Run: python3 tests/options_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll
"""
from pathlib import Path
import argparse, json, subprocess, tempfile, time, sqlite3, xml.etree.ElementTree as ET
from live_server_tests import free_port, request, Proxy

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(); p.add_argument('server_dll', type=Path); p.add_argument('--viewer', type=Path, default=ROOT / 'build/realm_client'); a = p.parse_args()
server_root = ROOT.parent / 'realm-server'; out = ROOT / 'build/tests/options-live'; out.mkdir(parents=True, exist_ok=True)
app, game = free_port(), free_port()
items = {o.get('id'): int(o.get('type'), 0) for o in ET.parse(server_root / 'Resources/GameData/EmbeddedData_EquipCXML.xml').getroot()}
classids = {o.get('id'): int(o.get('type'), 0) for o in ET.parse(server_root / 'Resources/GameData/EmbeddedData_PlayersCXML.xml').getroot()}
PLAYERSHOOT, USEITEM, MOVE, ESCAPE = 3, 13, 4, 42

with tempfile.TemporaryDirectory(prefix='realm-options-live-') as directory:
    work = Path(directory); settings = ET.parse(server_root / 'Settings.xml'); settings.getroot().set('res', str(server_root / 'Resources'))
    settings.getroot().set('db', str(work / 'Database')); settings.find('Ports').text = f'{app}:{game}'; settings.write(work / 'Settings.xml')
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
            name = 'FxOptions'; password = 'options-private-fixture'
            assert request(app, '/account/register', dict(newUsername=name, newPassword=password), '127.0.0.1').tag == 'Success'
            r = subprocess.run([str(a.viewer.resolve().parent / 'realm_session'), '--server', 'localhost', '--port', str(game), '--username', name, '--password-stdin', '--seconds', '1',
                                '--create', str(classids['Wizard'])], input=password + '\n', text=True, capture_output=True, timeout=15); assert r.returncode == 0, r.stdout + r.stderr
            deadline = time.monotonic() + 5
            while request(app, '/char/list', dict(username=name, password=password)).tag != 'Chars':
                assert time.monotonic() < deadline; time.sleep(.1)
            time.sleep(.5); db = sqlite3.connect(work / 'Database/realm.db')
            id = int(db.execute('SELECT value FROM kv WHERE path=?', ('login.username.' + name,)).fetchone()[0]); key = 'char.' + str(id) + '.0'
            char = ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?', (key,)).fetchone()[0])
            slots = list(map(int, char.findtext('Inventory').split(','))); slots[4:12] = [items['Magic Potion']] * 8; char.find('Inventory').text = ','.join(map(str, slots))
            db.execute('UPDATE kv SET value=? WHERE path=?', (ET.tostring(char, encoding='unicode'), key)); db.commit(); db.close()
            seeded = request(app, '/char/list', dict(username=name, password=password)); assert int(seeded.find('Char').findtext('Equipment').split(',')[4]) == items['Magic Potion']

            def play(label, script, values=None, game_id=None):
                """One client session through its own proxy; returns the packet counts by id."""
                file = out / f'{label}.settings.json'; file.write_text(json.dumps(dict(values=values or {})))
                (out / f'{label}.txt').write_text('\n'.join(script) + '\n')
                proxy = Proxy(game, 2 if game_id is not None else 1)
                r = subprocess.run([str(a.viewer.resolve()), '--connect', 'localhost', '--port', str(proxy.port), '--username', name, '--password-stdin', '--character', '0',
                                    '--hidden', '--script', str(out / f'{label}.txt'), '--settings-file', str(file)] + (['--game', str(game_id)] if game_id is not None else []),
                                   input=password + '\n', text=True, capture_output=True, timeout=40)
                counts = proxy.finish(); assert r.returncode == 0, r.stdout + r.stderr
                print(f'{label:34s} PLAYERSHOOT={counts[PLAYERSHOOT]:3d} USEITEM={counts[USEITEM]} MOVE={counts[MOVE]}')
                return counts

            # Autofire Toggle (I by default) fires without a mouse button; the mouse points into the map.
            fire = ['2500 move 300 200', '3000 key {key}', '5500 quit']
            on = play('autofire-default-key', [l.format(key='i') for l in fire]); assert on[PLAYERSHOOT] >= 3, on
            none = play('autofire-i-unbound', [l.format(key='i') for l in fire], dict(autofireToggle=85)); assert none[PLAYERSHOOT] == 0, none
            rebound = play('autofire-rebound-u', [l.format(key='u') for l in fire], dict(autofireToggle=85)); assert rebound[PLAYERSHOOT] >= 3, rebound
            # Keys are consumed while the Options screen is open, so autofire is not toggled behind it.
            modal = play('options-open-eats-keys', ['2500 move 300 200', '3000 key o', '3300 key i', '4500 key o', '6000 quit']); assert modal[PLAYERSHOOT] == 0, modal
            # Use/Equip Inventory Slots: slots 4-11 hold seeded potions (each use consumes one), so every case uses its own slot.
            used = play('slot2-default-key', ['3000 key 2', '5000 quit']); assert used[USEITEM] == 1, used
            gone = play('slot1-key-after-rebinding', ['3000 key 1', '5000 quit'], dict(useEquipInvSlot1=88)); assert gone[USEITEM] == 0, gone
            hot = play('slot1-rebound-x', ['3000 key x', '5000 quit'], dict(useEquipInvSlot1=88)); assert hot[USEITEM] == 1, hot
            # The same through the screen: Hot Keys tab, the Slot 3 row (grid cell 4), press Y, continue, then use it.
            via_ui = play('slot3-rebound-through-screen', ['2500 key o', '2700 click 230 78', '2900 click 60 226', '3100 key y', '3300 click 400 540', '3800 key y', '5000 quit'])
            assert via_ui[USEITEM] == 1, via_ui
            # Movement through a rebound key keeps the connection healthy (MOVE acknowledges every tick).
            walk = play('walk-rebound-j', ['2500 hold j', '4000 release j', '4500 quit'], dict(moveRight=74)); assert walk[MOVE] >= 10, walk
            # CharacterDetailsView: the Nexus has the Options button, any other map the Nexus button, which escapes.
            nexus = play('header-click-in-nexus', ['3000 click 780 210', '3500 click 400 540', '4500 quit']); assert nexus[ESCAPE] == 0, nexus
            tutorial = play('header-click-elsewhere', ['3000 click 780 210', '4500 quit'], game_id=-2); assert tutorial[ESCAPE] == 1, tutorial
            key = play('escape-key-elsewhere', ['3000 key Insert', '4500 quit'], game_id=-2); assert key[ESCAPE] == 1, key
            print('Isolated real-server Options bindings passed; saved account untouched')
        except BaseException:
            log.flush(); log.seek(0); (out / 'live-failure-server.log').write_text(log.read().replace('options-private-fixture', '[redacted]')); raise
        finally:
            server.terminate()
            try: server.wait(5)
            except subprocess.TimeoutExpired: server.kill(); server.wait()
