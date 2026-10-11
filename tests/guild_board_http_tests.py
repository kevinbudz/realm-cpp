#!/usr/bin/env python3
"""Native controller and form HTTP through an independent loopback app peer."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, unquote_plus
ROOT=Path(__file__).resolve().parents[1]
requests=[]
board='Initial board'
attempts=0
gets=0
mode='roundtrip'
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_POST(self):
        global board,attempts,gets
        fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(),keep_blank_values=True)
        requests.append((self.path,fields))
        assert fields['username']==['Fixture'] and fields['password']==['private-fixture']
        time.sleep(.15 if mode=='stale' else .015)
        if mode=='error': data='<Error>Not in guild</Error>'
        elif self.path=='/guild/getBoard':
            gets+=1;data=('stale reply' if gets==1 else 'fresh reply') if mode=='stale' else board
        else:
            assert self.path=='/guild/setBoard'
            attempts+=1
            if mode=='denied' or (mode=='retry' and attempts==1): data='<Error>No permission</Error>'
            else: board=unquote_plus(fields['board'][0]);data=board
        encoded=data.encode();self.send_response(200);self.send_header('Content-Length',str(len(encoded)));self.end_headers()
        # Fragment sized bodies as a real URLLoader peer might.
        for at in range(0,len(encoded),7): self.wfile.write(encoded[at:at+7]);self.wfile.flush()
with ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    for mode,rank in [('roundtrip',20),('roundtrip',10),('error',20),('denied',20),('retry',20),('stale',20)]:
        attempts=gets=0
        result=subprocess.run([sys.argv[1],str(ROOT/'build/fonts-client'),str(ROOT.parent/'realm-client'),mode,str(server.server_port),'Fixture',str(rank)],input='private-fixture\n',text=True,capture_output=True,timeout=15)
        assert result.returncode==0,result.stdout+result.stderr
        assert 'private-fixture' not in result.stdout+result.stderr
        if mode=='retry': assert attempts==2
        if rank<20: assert attempts==0
        print(result.stdout.strip())
    server.shutdown()
assert any(p=='/guild/setBoard' for p,_ in requests)
print('Native raw HTTP, fragmented UTF8, protected form encoding, permission, error Back and retry passed')
