#!/usr/bin/env python3
"""Exercise native creation, movement, and shooting against an isolated real server.

Requires a built RotMG.dll. The supplied server resources are read only; accounts
and characters exist solely in a temporary database. Credentials never appear
in subprocess arguments or test output.
"""
import argparse
from collections import Counter
from pathlib import Path
import select
import socket
import struct
import subprocess
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def request(port, path, fields):
    data = urllib.parse.urlencode(fields).encode()
    with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}{path}', data=data), timeout=2) as response:
        return ET.fromstring(response.read())


class Proxy:
    def __init__(self, server_port, connections=1):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.listener.settimeout(10)
        self.port = self.listener.getsockname()[1]
        self.server_port = server_port
        # RECONNECT reuses the original host and port, one connection per map.
        self.connections = connections
        self.counts = Counter()
        self.errors = []
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def run(self):
        try:
            for _ in range(self.connections):
                self.relay()
        except BaseException as error:
            self.errors.append(error)
        finally:
            self.listener.close()

    def relay(self):
        client, _ = self.listener.accept()
        with client, socket.create_connection(('127.0.0.1', self.server_port), timeout=5) as server:
            buffered = bytearray()
            while True:
                ready, _, _ = select.select([client, server], [], [], 10)
                if not ready:
                    raise TimeoutError('Native session stopped exchanging packets')
                for source in ready:
                    data = source.recv(65536)
                    if not data:
                        return
                    target = server if source is client else client
                    target.sendall(data)
                    if source is client:
                        buffered.extend(data)
                        while len(buffered) >= 5:
                            length = struct.unpack_from('>I', buffered)[0]
                            assert 5 <= length <= 1048576
                            if len(buffered) < length:
                                break
                            self.counts[buffered[4]] += 1
                            del buffered[:length]

    def finish(self):
        self.thread.join(12)
        assert not self.thread.is_alive(), 'Wire observer did not finish'
        if self.errors:
            raise self.errors[0]
        return self.counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('server_dll', type=Path)
    parser.add_argument('--client', type=Path, default=Path(__file__).resolve().parents[1] / 'build/realm_session')
    parser.add_argument('--server-root', type=Path, default=Path(__file__).resolve().parents[2] / 'realm-server')
    args = parser.parse_args()
    dll, client, root = args.server_dll.resolve(), args.client.resolve(), args.server_root.resolve()
    app_port, game_port = free_port(), free_port()
    credentials = {'username': 'NativeTest', 'password': 'native-fixture-password'}
    with tempfile.TemporaryDirectory(prefix='realm-native-session-') as directory:
        work = Path(directory)
        settings = ET.parse(root / 'Settings.xml')
        settings.getroot().set('res', str(root / 'Resources'))
        settings.getroot().set('db', str(work / 'Database'))
        settings.find('Ports').text = f'{app_port}:{game_port}'
        settings.write(work / 'Settings.xml')
        with (work / 'server.log').open('w+') as log:
            server = subprocess.Popen(['dotnet', str(dll)], cwd=work, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 45
                while True:
                    if server.poll() is not None:
                        log.seek(0)
                        raise RuntimeError('Test server exited:\n' + log.read())
                    try:
                        ready = request(app_port, '/char/list', {})
                        if ready.tag == 'Chars':
                            break
                    except (OSError, TimeoutError):
                        pass
                    if time.monotonic() >= deadline:
                        raise TimeoutError('Test server did not become ready')
                    time.sleep(.1)
                registered = request(app_port, '/account/register', {'newUsername': credentials['username'], 'newPassword': credentials['password']})
                assert registered.tag == 'Success', 'Fixture account registration failed'
                counts = Counter()
                for choice, seconds in [(['--create', '0x030e'], 2), (['--character', '0', '--exercise'], 6), (['--character', '0', '--exercise-inventory'], 6), (['--character', '0', '--exercise-potion'], 3), (['--character', '0', '--exercise-portal'], 10)]:
                    proxy = Proxy(game_port, 2 if '--exercise-portal' in choice else 1)
                    result = subprocess.run([str(client), '--server', 'localhost', '--port', str(proxy.port), '--username', credentials['username'], '--password-stdin', '--seconds', str(seconds)] + choice,
                                            input=credentials['password'] + '\n', text=True, capture_output=True, timeout=15)
                    counts.update(proxy.finish())
                    assert result.returncode == 0, result.stdout + result.stderr
                    assert 'Playing character=0 map=Nexus' in result.stdout, result.stdout
                    if '--exercise-portal' in choice:
                        assert 'Interaction target Vault Portal entered' in result.stdout, result.stdout
                        assert 'Real server USEPORTAL reconnected to map=Vault' in result.stdout, result.stdout
                    else:
                        assert 'World 142x130 objects=' in result.stdout, result.stdout
                    assert credentials['password'] not in result.stdout + result.stderr
                    if '--exercise-inventory' in choice:
                        assert 'Real server inventory SWAP, consumable USEITEM, DROP, container pickup and re-equip accepted' in result.stdout, result.stdout
                    if '--exercise-potion' in choice:
                        assert 'Real server magic potion USEITEM accepted; stack 6 -> 5' in result.stdout, result.stdout
                assert counts[15] == 6 and counts[2] == 1 and counts[21] == 5 and counts[23] == 1, counts
                assert counts[4] >= 50, f'Too few MOVE acknowledgments: {counts[4]}'
                assert counts[3] >= 5, f'Too few PLAYERSHOOT messages: {counts[3]}'
                assert counts[13] >= 1, 'Ability USEITEM message missing'
                assert counts[31] >= 1, 'Server ability projectiles were not acknowledged'
                assert counts[12] == 3 and counts[17] == 1, f'Inventory swap/drop packets missing: {counts[12]} INVSWAP, {counts[17]} INVDROP'
                print(f'Real server passed: CREATE, LOAD, {counts[4]} MOVE, {counts[3]} PLAYERSHOOT, {counts[13]} USEITEM, {counts[31]} SHOOTACK, {counts[12]} INVSWAP and {counts[17]} INVDROP packets, including container pickup, magic potion use and vault portal entry; all five sessions remained connected')
            finally:
                server.terminate()
                try:
                    server.wait(5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == '__main__':
    main()
