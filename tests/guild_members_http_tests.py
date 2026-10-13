#!/usr/bin/env python3
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import parse_qs
import subprocess,sys,threading,time
ROOT=Path(__file__).resolve().parents[1]
xml='<Members><CurrentFame>321</CurrentFame><GuildLevel>2</GuildLevel><Member><Name>Zed café &amp; +</Name><Rank>0</Rank><Fame>25</Fame></Member><Member><Name>Abe</Name><Rank>20</Rank><Fame>91</Fame></Member></Members>'
for mode in ['load','error','retry','stale','malformed']:
 posts=[];errors=[]
 class Handler(BaseHTTPRequestHandler):
  def log_message(self,*a):pass
  def do_POST(self):
   try:
    fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode());posts.append(fields)
    assert self.path=='/guild/listMembers'
    assert fields==dict(username=['Fixture + &'],password=['http-private + % café'],num=['50'],offset=['0'])
    n=len(posts);time.sleep(.2 if mode=='stale' and n==1 else .015)
    reply='<Error>Not in guild</Error>' if mode=='error' or mode=='retry' and n==1 else '<Members>' if mode=='malformed' or mode=='stale' and n==1 else xml
    data=reply.encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
    for i in range(0,len(data),7):self.wfile.write(data[i:i+7]);self.wfile.flush()
   except BaseException as e:errors.append(e)
 with ThreadingHTTPServer(('127.0.0.1',0),Handler) as server:
  thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
  result=subprocess.run([sys.argv[1],'http','error' if mode=='malformed' else mode,str(server.server_port),'Fixture + &','40'],input='http-private + % café\n',capture_output=True,text=True,timeout=16)
  server.shutdown();thread.join()
  assert result.returncode==0,result.stdout+result.stderr
  if errors:raise errors[0]
  assert len(posts)==(2 if mode in ['retry','stale'] else 1),posts
  print(result.stdout.strip())
