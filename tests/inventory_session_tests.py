#!/usr/bin/env python3
"""Independent wire peer checks inventory actions through the real Session."""
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading

utf = lambda value: struct.pack('>H', len(value.encode())) + value.encode()
frame = lambda ident, body: struct.pack('>IB', len(body)+5, ident) + body
def exact(conn, length):
    data = bytearray()
    while len(data) < length:
        chunk = conn.recv(length-len(data)); assert chunk, 'Unexpected disconnect'
        data.extend(chunk)
    return bytes(data)
def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]; assert 5 <= length <= 1048576
    data = exact(conn, length-4); return data[0], data[1:]
errors = []
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0)); listener.listen(1)
    def server():
        try:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(5)
                ident, _ = receive(conn); assert ident == 15, 'HELLO'
                conn.sendall(frame(20, struct.pack('>ii', 8,8) + utf('Nexus') + utf('Nexus') + struct.pack('>Ii??', 77,0,True,True)))
                ident, body = receive(conn); assert ident == 21 and body == struct.pack('>i', 7), 'LOAD'
                stats = {8:0xa00, 9:0xa08, 12:0xa00, 14:0xa22, 71:222, 75:111, 77:333, 4:10000}
                status = struct.pack('>iffB', 99,2.5,3.5,len(stats)) + b''.join(struct.pack('>Bi', key,value) for key,value in stats.items())
                update = struct.pack('>hhH', 0,1,0x031e) + status + struct.pack('>h', 0)
                conn.sendall(frame(1, struct.pack('>ii',99,7)) + frame(9, update))
                ident, body = receive(conn); assert ident == 12, 'INVSWAP'
                time,x,y,a,slot_a,b,slot_b = struct.unpack('>iffiBiB', body)
                assert time >= 0 and (x,y,a,slot_a,b,slot_b) == (2.5,3.5,99,4,99,0), 'Swap wire position and slots'
                failure = frame(18, struct.pack('>i',1)); conn.sendall(failure[:3]); conn.sendall(failure[3:])
                ident, body = receive(conn); assert ident == 13, 'Consumable USEITEM'
                time,owner,slot,x,y,use = struct.unpack('>iiBffB',body); assert (owner,slot,x,y,use) == (99,6,0,0,0), 'Consumable wire target'
                conn.sendall(failure)
                ident, body = receive(conn); assert ident == 13, 'Ability USEITEM'
                time,owner,slot,x,y,use = struct.unpack('>iiBffB',body); assert (owner,slot,x,y,use) == (99,1,4,4,0), 'Ability wire target'
                ident, body = receive(conn); assert ident == 3 and len(body) in (17,18) and body[16] == 1, 'Ability PLAYERSHOOT follows USEITEM'
                conn.sendall(frame(18, struct.pack('>i',0)))
                ident, body = receive(conn); assert ident == 17 and body == bytes([4]), 'INVDROP'
                conn.sendall(failure)
                while conn.recv(65536):
                    pass
        except BaseException as error:
            errors.append(error)
    worker = threading.Thread(target=server, daemon=True); worker.start()
    result = subprocess.run([str(Path(sys.argv[1]).resolve()), str(listener.getsockname()[1])], capture_output=True, text=True, timeout=10)
    worker.join(timeout=6)
    assert not worker.is_alive(), 'Wire peer did not terminate'
    if errors:
        raise errors[0]
    assert result.returncode == 0, result.stdout + result.stderr
    print(result.stdout.strip())
