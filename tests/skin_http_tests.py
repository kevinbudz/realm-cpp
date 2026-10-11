#!/usr/bin/env python3
"""Independent HTTP peer validates skin credentials, ownership and stale purchases."""
import subprocess,sys,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import parse_qs
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];mode='success';posts=[];errors=[]
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_POST(self):
  try:
   fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode());posts.append(self.path)
   assert self.headers['Host']==f'localhost:{self.server.server_port}'
   assert fields['username']==['Fixture'] and fields['password']==['private-fixture']
   if self.path=='/char/list':data='<Chars><Account><Stats><Credits>1000</Credits></Stats></Account><OwnedSkins>835</OwnedSkins></Chars>'
   else:
    assert self.path=='/account/purchaseSkin' and fields['skinType']==['836'];time.sleep(.1)
    data='<Error>Account in use!</Error>' if mode=='failure' else '<Success/>'
   b=data.encode();self.send_response(200);self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
  except BaseException as e:errors.append(e)
with ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
 t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
 for mode in ['success','failure','stale','other']:
  posts=[];r=subprocess.run([sys.argv[1],str(ROOT/'build/fonts-client'),str(ROOT/'build/hud-art'),mode,str(server.server_port)],capture_output=True,text=True,timeout=12)
  assert r.returncode==0,r.stdout+r.stderr
  assert posts==['/char/list','/account/purchaseSkin'],posts
  print(r.stdout.strip())
 server.shutdown()
if errors:raise errors[0]
