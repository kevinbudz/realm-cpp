#!/usr/bin/env python3
"""Independent TCP peer verifies social requests and login after a rename."""
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
        chunk = conn.recv(length-len(data))
        assert chunk, 'Unexpected disconnect'
        data.extend(chunk)
    return bytes(data)

def receive(conn):
    length = struct.unpack('>I', exact(conn, 4))[0]
    assert 5 <= length <= 1048576
    body = exact(conn, length-4)
    return body[0], body[1:]

def status(guild):
    return struct.pack('>iffB', 99, 3., 3., 2) + bytes([28]) + utf('Renamed') + bytes([56]) + utf(guild)

def map_info(name):
    return frame(20, struct.pack('>ii', 8,8) + utf(name) + utf(name) + struct.pack('>Ii??', 77,0,True,True))

errors = []
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen(2)
    listener.settimeout(6)
    def server():
        try:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(5)
                ident, body = receive(conn)
                assert ident == 15 and body == utf('1.0.0')+struct.pack('>i',-1)+utf('fixture')+utf('test-password')+struct.pack('>i',0), 'Initial HELLO'
                conn.sendall(map_info('Nexus'))
                assert receive(conn) == (21, struct.pack('>i',7)), 'LOAD'
                update = struct.pack('>hhH', 0,1,0x030e)+status('Native Guild')+struct.pack('>h',0)
                conn.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
                assert receive(conn) == (49,utf('Taken')), 'CHOOSENAME'
                rejection=frame(50,b'\x00'+utf('Name already taken.'))
                conn.sendall(rejection[:3]);conn.sendall(rejection[3:])
                assert receive(conn) == (49,utf('Renamed')), 'Name retry / no duplicate submission'
                conn.sendall(frame(50,b'\x01'+utf('')))
                assert receive(conn) == (36,utf('Bad Guild')), 'CREATEGUILD'
                conn.sendall(frame(37,b'\x00'+utf('Guild name unavailable')))
                assert receive(conn) == (36,utf('Native Guild')), 'Guild retry'
                conn.sendall(frame(37,b'\x01'+utf('Success!')))
                assert receive(conn) == (38,utf('Renamed')), 'GUILDREMOVE self'
                conn.sendall(frame(11,struct.pack('>h',1)+status('')+bytes([0])))
                assert receive(conn) == (23,struct.pack('>i',55)), 'Guild hall USEPORTAL'
                conn.sendall(frame(19,struct.pack('>i',-10)))
                while conn.recv(65536): pass
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(5)
                ident, body = receive(conn)
                assert ident == 15 and body == utf('1.0.0')+struct.pack('>i',-10)+utf('Renamed')+utf('test-password')+struct.pack('>i',0), 'Reconnect uses renamed login'
                conn.sendall(map_info('Guild Hall'))
                assert receive(conn) == (21,struct.pack('>i',7)), 'Reload same character'
                conn.sendall(frame(1,struct.pack('>ii',99,7)))
                while conn.recv(65536): pass
        except BaseException as error:
            errors.append(error)
    worker=threading.Thread(target=server,daemon=True)
    worker.start()
    result=subprocess.run([str(Path(sys.argv[1]).resolve()),str(listener.getsockname()[1])],capture_output=True,text=True,timeout=10)
    worker.join(6)
    assert not worker.is_alive(), 'Wire peer did not finish'
    if errors: raise errors[0]
    assert result.returncode==0, result.stdout+result.stderr
    print(result.stdout.strip())
