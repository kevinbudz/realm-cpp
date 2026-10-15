#!/usr/bin/env python3
"""Exercise native creation, movement, and shooting against an isolated real server.

Requires a built RotMG.dll. The supplied server resources are read only; accounts
and characters exist solely in a temporary database. Credentials never appear
in subprocess arguments or test output.
"""
import argparse
import http.client
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


def request(port, path, fields, source='127.0.0.1'):
    # The server allows one registration per address; further fixture
    # accounts register from other loopback addresses.
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10, source_address=(source, 0))
    try:
        connection.request('POST', path, urllib.parse.urlencode(fields), {'Content-Type': 'application/x-www-form-urlencoded'})
        return ET.fromstring(connection.getresponse().read())
    finally:
        connection.close()


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
                    try:
                        data = source.recv(65536)
                    except ConnectionResetError:
                        # A peer closing with unread input resets; each session's
                        # own result is asserted separately.
                        return
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
    parser.add_argument('--client', type=Path, default=Path(__file__).resolve().parents[1] / 'build/tests/realm_session')
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
                for choice, seconds in [(['--create', '0x030e'], 2), (['--character', '0', '--exercise'], 6), (['--character', '0', '--exercise-inventory'], 6), (['--character', '0', '--exercise-purchase'], 4), (['--character', '0', '--exercise-potion'], 3), (['--character', '0', '--exercise-portal'], 10)]:
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
                    if '--exercise-purchase' in choice:
                        assert 'Real server BUYRESULT 1 opened and closed native Purchase Error; pending purchase released' in result.stdout, result.stdout
                    if '--exercise-potion' in choice:
                        assert 'Real server magic potion USEITEM accepted; stack 6 -> 5' in result.stdout, result.stdout
                # PlayerMenu: a second fixture player stays in the Nexus while the
                # first teleports to it, locks/ignores it and requests a trade.
                friend = {'username': 'NativeFriend', 'password': 'native-friend-password'}
                assert request(app_port, '/account/register', {'newUsername': friend['username'], 'newPassword': friend['password']}, '127.0.0.2').tag == 'Success', 'Second fixture account registration failed'
                friend_command = [str(client), '--server', 'localhost', '--username', friend['username'], '--password-stdin']
                proxy = Proxy(game_port)
                result = subprocess.run(friend_command + ['--port', str(proxy.port), '--seconds', '2', '--create', '0x0300'], input=friend['password'] + '\n', text=True, capture_output=True, timeout=15)
                proxy.finish()
                assert result.returncode == 0, result.stdout + result.stderr
                friend_proxy, menu_proxy = Proxy(game_port), Proxy(game_port)
                idle = subprocess.Popen(friend_command + ['--port', str(friend_proxy.port), '--seconds', '13', '--character', '0'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                idle.stdin.write(friend['password'] + '\n'); idle.stdin.close()
                result = subprocess.run([str(client), '--server', 'localhost', '--port', str(menu_proxy.port), '--username', credentials['username'], '--password-stdin', '--seconds', '10', '--character', '0', '--exercise-menu'],
                                        input=credentials['password'] + '\n', text=True, capture_output=True, timeout=20)
                menu_counts = menu_proxy.finish()
                idle_out, idle_err = idle.communicate(timeout=20); friend_proxy.finish()
                assert result.returncode == 0, result.stdout + result.stderr
                assert idle.returncode == 0, idle_out + idle_err
                assert 'Real server PlayerMenu TELEPORT, EDITACCOUNTLIST lock/unlock/ignore/unignore and REQUESTTRADE accepted' in result.stdout, result.stdout
                assert 'Real server PLAYERTEXT echoed from ' in result.stdout, result.stdout
                assert menu_counts[22] == 1 and menu_counts[33] == 4 and menu_counts[51] == 1 and menu_counts[5] == 1, menu_counts
                assert friend['password'] not in result.stdout + result.stderr + idle_out + idle_err
                # TradePanel: the friend accepts the request and both accept after the
                # countdown. Fresh fixtures hold only soulbound gear, so nothing is offered.
                trade_proxies = [Proxy(game_port), Proxy(game_port)]
                responder = subprocess.Popen(friend_command + ['--port', str(trade_proxies[1].port), '--seconds', '12', '--character', '0', '--exercise-trade', credentials['username']], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                responder.stdin.write(friend['password'] + '\n'); responder.stdin.close()
                result = subprocess.run([str(client), '--server', 'localhost', '--port', str(trade_proxies[0].port), '--username', credentials['username'], '--password-stdin', '--seconds', '12', '--character', '0', '--exercise-trade', friend['username'], '--trade-initiator'],
                                        input=credentials['password'] + '\n', text=True, capture_output=True, timeout=25)
                trade_counts = trade_proxies[0].finish()
                responder_out, responder_err = responder.communicate(timeout=25); trade_proxies[1].finish()
                assert result.returncode == 0, result.stdout + result.stderr
                assert responder.returncode == 0, responder_out + responder_err
                for output in (result.stdout, responder_out):
                    assert 'Real server trade completed: Trade Successful!' in output, output
                assert trade_counts[51] == 1 and trade_counts[56] == 1, trade_counts
                assert counts[15] == 7 and counts[2] == 1 and counts[21] == 6 and counts[23] == 1, counts
                assert counts[25] == 1, f'One outstanding BUY expected: {counts[25]}'
                assert counts[4] >= 50, f'Too few MOVE acknowledgments: {counts[4]}'
                assert counts[3] >= 5, f'Too few PLAYERSHOOT messages: {counts[3]}'
                assert counts[13] >= 1, 'Ability USEITEM message missing'
                assert counts[31] >= 1, 'Server ability projectiles were not acknowledged'
                assert counts[12] == 3 and counts[17] == 1, f'Inventory swap/drop packets missing: {counts[12]} INVSWAP, {counts[17]} INVDROP'
                print(f'Real server passed: CREATE, LOAD, {counts[4]} MOVE, {counts[3]} PLAYERSHOOT, {counts[13]} USEITEM, {counts[31]} SHOOTACK, {counts[12]} INVSWAP and {counts[17]} INVDROP packets, including container pickup, magic potion use and vault portal entry; all six sessions remained connected, including BUY / insufficient-funds BUYRESULT 1 and native Purchase Error close. PlayerMenu: 1 TELEPORT, 4 EDITACCOUNTLIST, 1 REQUESTTRADE and 1 PLAYERTEXT accepted between two fixture players, and a completed (empty) trade')
            finally:
                server.terminate()
                try:
                    server.wait(5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == '__main__':
    main()
