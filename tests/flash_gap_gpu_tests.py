#!/usr/bin/env python3
"""Loopback UI checks for quest arrows, guild invites, Locker keys and Tutorial drawings."""
import argparse
import json
import select
import socket
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary-dir', type=Path, default=ROOT / 'build/tests')
parser.add_argument('--cases', default='guild,locker,quest,tutorial')
parser.add_argument('--output', type=Path, default=ROOT / 'build/tests/flash-gap-gpu')
args = parser.parse_args()
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)

HELLO, CREATE_SUCCESS, UPDATE, MAPINFO, LOAD = 15, 1, 9, 20, 21
DEATH, QUESTOBJID, INVITEDTOGUILD, JOINGUILD = 24, 35, 43, 44
TRADEREQUESTED, GLOBAL_NOTIFICATION = 52, 60
utf = lambda value: struct.pack('>H', len(value.encode())) + value.encode()
frame = lambda ident, body: struct.pack('>IB', len(body) + 5, ident) + body


def exact(conn, length):
    result = bytearray()
    while len(result) < length:
        part = conn.recv(length - len(result))
        if not part:
            raise EOFError('client closed the loopback connection')
        result.extend(part)
    return bytes(result)


def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    assert 5 <= length <= 1048576, f'invalid packet length {length}'
    body = exact(conn, length - 4)
    return body[0], body[1:]


def status(object_id, x, y, name=''):
    stats = (bytes([28]) + utf(name)) if name else b''
    return struct.pack('>iffB', object_id, x, y, 1 if name else 0) + stats


def update(objects):
    body = struct.pack('>h', 0) + struct.pack('>h', len(objects))
    for object_type, object_status in objects:
        body += struct.pack('>H', object_type) + object_status
    return body + struct.pack('>h', 0)


def map_info(name, display_name=None, seed=1):
    display_name = display_name if display_name is not None else name
    return (struct.pack('>ii', 100, 100) + utf(name) + utf(display_name) + struct.pack('>Ii??', seed, 0, True, True))


def load_map(conn, name, display_name=None, seed=1, objects=None):
    conn.sendall(frame(MAPINFO, map_info(name, display_name, seed)))
    ident, body = receive(conn)
    assert ident == LOAD and body == struct.pack('>i', 7), f'expected LOAD after MAPINFO, got {ident}, {body!r}'
    conn.sendall(frame(CREATE_SUCCESS, struct.pack('>ii', 99, 7)))
    if objects is None:
        objects = [(0x030e, status(99, 50, 50, 'Fixture'))]
    conn.sendall(frame(UPDATE, update(objects)))


def global_notification(text):
    return frame(GLOBAL_NOTIFICATION, struct.pack('>i', 0) + utf(text))


def invite(name, guild):
    return frame(INVITEDTOGUILD, utf(name) + utf(guild))


def changed_pixels(a, b, box):
    left = Image.open(a).convert('RGB').crop(box)
    right = Image.open(b).convert('RGB').crop(box)
    diff = ImageChops.difference(left, right)
    return sum(1 for pixel in diff.getdata() if pixel != (0, 0, 0))


def capture(ms, name):
    path = (OUT / f'{name}.png').resolve()
    return f'{ms} capture {path}'


def run_client(name, settings, actions, after_setup, timeout=12, initial_map='Nexus'):
    errors, packets = [], []
    with tempfile.TemporaryDirectory(prefix='realm-gap-') as temp, socket.socket() as listener:
        directory = Path(temp)
        settings_path = directory / 'settings.json'
        settings_path.write_text(json.dumps({'values': settings, 'cameraAngle': 0}))
        for action in actions:
            parts = action.split(' capture ', 1)
            if len(parts) == 2:
                Path(parts[1]).unlink(missing_ok=True)
        script_path = directory / 'actions.txt'
        script_path.write_text('\n'.join(actions) + '\n')
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(timeout)

        def peer():
            try:
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(timeout)
                    ident, _ = receive(conn)
                    assert ident == HELLO, f'expected HELLO, got {ident}'
                    load_map(conn, initial_map, initial_map)
                    after_setup(conn, packets)
                    # Keep the server alive until the scripted quit; collect any client decisions.
                    deadline = time.monotonic() + timeout
                    while time.monotonic() < deadline:
                        ready, _, _ = select.select([conn], [], [], 0.2)
                        if not ready:
                            continue
                        try:
                            ident, body = receive(conn)
                        except EOFError:
                            return
                        packets.append((ident, body))
            except BaseException as error:
                errors.append(error)

        thread = threading.Thread(target=peer, daemon=True)
        thread.start()
        result = subprocess.run(
            [str(args.binary_dir.resolve().parent / 'realm_client'), '--connect', '127.0.0.1', '--port',
             str(listener.getsockname()[1]), '--username', 'Fixture', '--character', '7', '--script',
             str(script_path), '--settings-file', str(settings_path), '--hidden', '--benchmark', '--audio', 'off',
             '--frames', '1000000'],
            cwd=ROOT, capture_output=True, text=True, timeout=timeout + 3)
        thread.join(timeout=3)
        assert not thread.is_alive(), f'{name}: loopback server did not finish'
        if errors:
            raise errors[0]
        assert result.returncode == 0, f'{name}: {result.stdout}\n{result.stderr}'
        (OUT / f'{name}.log').write_text(result.stdout + result.stderr)
    return packets


def collect_for(conn, packets, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ready, _, _ = select.select([conn], [], [], min(.2, end - time.monotonic()))
        if not ready:
            continue
        try:
            packets.append(receive(conn))
        except EOFError:
            return


def guild_invite_cases():
    panel = (600, 500, 800, 600)

    def one_invite(conn, packets):
        conn.sendall(invite('GuildFriend', 'Fixture Guild'))
        collect_for(conn, packets, 3)

    accepted = run_client('guild-accept', {'showGuildInvitePopup': 1},
                          [capture(700, 'guild-accept-open'), '1200 click 750 567',
                           capture(1900, 'guild-accept-closed'), '2200 quit'], one_invite)
    joins = [body for ident, body in accepted if ident == JOINGUILD]
    assert joins == [utf('Fixture Guild')], f'accept should send one JOINGUILD with the offered name: {joins}'
    assert (changed_pixels(OUT / 'guild-accept-open.png', OUT / 'guild-accept-closed.png', panel) > 100), \
        'accept should close the invite panel'

    rejected = run_client('guild-reject', {'showGuildInvitePopup': 1},
                          [capture(700, 'guild-reject-open'), '1200 click 653 567',
                           capture(1900, 'guild-reject-closed'), '2200 quit'], one_invite)
    assert not [body for ident, body in rejected if ident == JOINGUILD], 'reject must not send JOINGUILD'
    assert (changed_pixels(OUT / 'guild-reject-open.png', OUT / 'guild-reject-closed.png', panel) > 100), \
        'reject should close the invite panel'

    timed_out = run_client('guild-timeout', {'showGuildInvitePopup': 1},
                           [capture(700, 'guild-timeout-open'), capture(21000, 'guild-timeout-closed'),
                            '21300 quit'], one_invite, timeout=25)
    assert not [body for ident, body in timed_out if ident == JOINGUILD], 'timeout must not join the guild'
    assert (changed_pixels(OUT / 'guild-timeout-open.png', OUT / 'guild-timeout-closed.png', panel) > 100), \
        'the 20-second invite timer should close the panel'

    def delayed_invite(conn, packets):
        time.sleep(1.6)
        conn.sendall(invite('GuildFriend', 'Fixture Guild'))
        collect_for(conn, packets, 2)

    off = run_client('guild-popup-off', {'showGuildInvitePopup': 0},
                     [capture(700, 'guild-popup-off-before'), capture(2500, 'guild-popup-off-after'),
                      '3000 quit'], delayed_invite)
    assert not [body for ident, body in off if ident == JOINGUILD], 'popup-off invitation is chat-only'
    assert (changed_pixels(OUT / 'guild-popup-off-before.png', OUT / 'guild-popup-off-after.png', panel) < 500), \
        'showGuildInvitePopup off should leave the override panel unchanged'

    def override(conn, packets):
        conn.sendall(invite('GuildFriend', 'First Guild'))
        time.sleep(1.4)
        conn.sendall(frame(TRADEREQUESTED, utf('TradeFriend')))
        time.sleep(1.1)
        conn.sendall(invite('GuildFriend', 'Second Guild'))
        collect_for(conn, packets, 3)

    run_client('guild-trade-override', {'showGuildInvitePopup': 1, 'showTradePopup': 1},
               [capture(700, 'override-invite-first'), capture(2200, 'override-trade-request'),
                capture(3700, 'override-invite-second'), '4000 quit'], override)
    assert (changed_pixels(OUT / 'override-invite-first.png', OUT / 'override-trade-request.png', panel) > 100), \
        'a trade request replaces an open guild invitation'
    assert (changed_pixels(OUT / 'override-trade-request.png', OUT / 'override-invite-second.png', panel) > 100), \
        'a new guild invitation replaces the trade request'


def wait_capture(name, timeout=10):
    deadline = time.monotonic() + timeout
    while not (OUT / f'{name}.png').exists():
        if time.monotonic() >= deadline:
            raise AssertionError(f'client did not reach capture {name}')
        time.sleep(.01)


def locker_cases():
    def map_exit(conn, packets):
        wait_capture('locker-before-key')
        conn.sendall(global_notification('showKeyUI'))
        wait_capture('locker-blank')
        conn.sendall(global_notification('purple'))
        wait_capture('locker-lit')
        load_map(conn, 'Nexus', 'Nexus', 3)
        collect_for(conn, packets, 2)

    run_client('locker-keys', {}, [capture(2000, 'locker-before-key'), capture(3000, 'locker-blank'),
                                   capture(4000, 'locker-lit'), capture(5500, 'locker-left-map'), '6000 quit'],
               map_exit, timeout=10, initial_map="Davy Jones's Locker")

    box = (4, 4, 180, 40)
    assert (changed_pixels(OUT / 'locker-before-key.png', OUT / 'locker-blank.png', box) > 50), \
        'showKeyUI opens the panel in the Locker'
    assert (changed_pixels(OUT / 'locker-blank.png', OUT / 'locker-lit.png', box) > 20), \
        'the purple notification changes the displayed key art'
    assert (changed_pixels(OUT / 'locker-lit.png', OUT / 'locker-left-map.png', box) > 50), \
        'leaving the Locker clears the panel'

    def death(conn, packets):
        wait_capture('locker-death-before-key')
        conn.sendall(global_notification('showKeyUI'))
        wait_capture('locker-death-blank')
        conn.sendall(global_notification('purple'))
        wait_capture('locker-lit-before-death')
        conn.sendall(frame(DEATH, struct.pack('>ii', 42, 7) + utf('Fixture boss')))
        collect_for(conn, packets, 2)

    run_client('locker-death', {}, [capture(2000, 'locker-death-before-key'), capture(3000, 'locker-death-blank'),
                                    capture(4000, 'locker-lit-before-death'),
                                    capture(5500, 'locker-death-reset'), '6000 quit'], death, timeout=10,
               initial_map="Davy Jones's Locker")
    assert (changed_pixels(OUT / 'locker-lit-before-death.png', OUT / 'locker-death-reset.png', box) > 50), \
        'DEATH clears the key panel in direct-connect mode'


def quest_case():
    def quest_peer(conn, packets):
        # Replace the initial static world with a player and an off-screen enemy target.
        conn.sendall(frame(UPDATE, update([(0x030e, status(99, 50, 50, 'Fixture')),
                                          (0x023b, status(7, 80, 50, ''))])) +
                     frame(QUESTOBJID, struct.pack('>i', 7)))
        collect_for(conn, packets, 8)

    run_client('quest-arrow', {'showQuestPortraits': 1},
               [capture(900, 'quest-before-delay'), '4500 move 590 325', capture(5100, 'quest-new-tooltip'),
                '6300 move 100 400', capture(7200, 'quest-portrait-tooltip'), '7600 quit'], quest_peer, timeout=10)
    arrow = (560, 290, 640, 360)
    assert (changed_pixels(OUT / 'quest-before-delay.png', OUT / 'quest-new-tooltip.png', arrow) > 20), \
        'QUESTOBJID draws its arrow after the four-second delay'
    tip = (450, 200, 600, 450)
    assert (changed_pixels(OUT / 'quest-new-tooltip.png', OUT / 'quest-portrait-tooltip.png', tip) > 100), \
        'hover shows the Quest tooltip, then rollout shows the portrait tooltip after its new timer'


def tutorial_case():
    sign = 0x01c9

    def tutorial_peer(conn, packets):
        wait_capture('tutorial-no-sign')
        conn.sendall(frame(UPDATE, update([(sign, status(10, 51, 50, 'Check out the|minimap.'))])))
        collect_for(conn, packets, 3)

    run_client('tutorial-sign', {}, [capture(2000, 'tutorial-no-sign'), capture(3500, 'tutorial-sign-near'),
                                     '4000 quit'], tutorial_peer, initial_map='Tutorial')
    drawing = (600, 0, 800, 220)
    assert (changed_pixels(OUT / 'tutorial-no-sign.png', OUT / 'tutorial-sign-near.png', drawing) > 100), \
        'a nearby named tutorial Sign activates the right-anchored Tutorial drawing overlay'


suites = {'guild': guild_invite_cases, 'locker': locker_cases, 'quest': quest_case, 'tutorial': tutorial_case}
for case in args.cases.split(','):
    suites[case]()
print('Guild invite accept/reject/timeout/popups-off and trade override, Locker show/light/map/death reset, '
      'quest arrow/tooltips and Tutorial Sign proximity passed')
