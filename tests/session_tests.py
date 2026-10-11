#!/usr/bin/env python3
"""Independent loopback server validates native handshake, framing and acknowledgments."""
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading

exe = Path(sys.argv[1]).resolve()
errors = []
utf = lambda value: struct.pack('>H', len(value.encode())) + value.encode()
frame = lambda ident, body: struct.pack('>IB', len(body) + 5, ident) + body

def exact(conn, length):
    result = bytearray()
    while len(result) < length:
        part = conn.recv(length - len(result))
        if not part:
            raise AssertionError('Native client disconnected early')
        result.extend(part)
    return bytes(result)

def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    assert 5 <= length <= 1048576
    body = exact(conn, length - 4)
    return body[0], body[1:]

with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    def server():
        try:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(5)
                ident, body = receive(conn)
                assert ident == 15
                assert body == utf('1.0.0') + struct.pack('>i', -1) + utf('fixture') + utf('test-password') + struct.pack('>i', 0), 'HELLO wire bytes'
                info = struct.pack('>ii', 8, 8) + utf('Nexus') + utf('Loopback Nexus') + struct.pack('>Ii??', 77, 0, True, True)
                packet = frame(20, info)
                # Deliberately fragment the packet length, ID and payload.
                for end in (1, 3, 5, 11):
                    conn.sendall(packet[:end]); packet = packet[end:]
                conn.sendall(packet)
                ident, body = receive(conn)
                assert ident == 21 and body == struct.pack('>i', 7), 'LOAD after MAPINFO'
                status = lambda oid, x, y, hp: struct.pack('>iffBBi', oid, x, y, 1, 1, hp)
                update = struct.pack('>hhhh', 1, 2, 2, 0x48) + struct.pack('>hH', 2, 0x030e) + status(99, 2.5, 2.5, 80) + struct.pack('>H', 0x030e) + status(88, 2.5, 2.5, 42) + struct.pack('>h', 0)
                tick = struct.pack('>h', 1) + status(88, 4.5, 2.5, 43) + struct.pack('>BBi', 1, 1, 79)
                conn.sendall(frame(1, struct.pack('>ii', 99, 7)) + frame(9, update) + frame(11, tick) + frame(61, struct.pack('>i', 1234)) + frame(16, struct.pack('>iff', 99, 3.5, 2.5)))
                ack = {}
                for _ in range(3):
                    ident, body = receive(conn); ack[ident] = body
                serial, time_ms = struct.unpack('>ii', ack[62]); assert serial == 1234 and 0 <= time_ms <= 1000, 'PONG serial/time'
                assert struct.unpack('>i', ack[48])[0] == time_ms, 'GOTOACK frame time'
                move_time, x, y = struct.unpack('>iff', ack[4]); assert move_time == time_ms and x == 3.5 and y == 2.5, 'MOVE acknowledges NEWTICK with predicted position'
                # Keep connection alive until the finite client run ends.
                while conn.recv(65536):
                    pass
        except BaseException as error:
            errors.append(error)
    thread = threading.Thread(target=server); thread.start()
    result = subprocess.run([str(exe), '--server', 'localhost', '--port', str(listener.getsockname()[1]), '--username', 'fixture', '--password-stdin', '--character', '7', '--seconds', '1'], input='test-password\n', text=True, capture_output=True, timeout=10)
    thread.join(timeout=6)
    assert not thread.is_alive(), 'Stub server did not finish'
    if errors:
        raise errors[0]
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'Playing character=7 map=Loopback Nexus' in result.stdout, result.stdout
    assert 'World 8x8 objects=2' in result.stdout, result.stdout
    assert 'test-password' not in result.stdout + result.stderr
print('Loopback handshake, partial/coalesced frames, world updates, PONG and GOTOACK passed')
