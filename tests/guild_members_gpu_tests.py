#!/usr/bin/env python3
"""Actual SDL GuildChroniclePanel/HTTP/member confirmation/TCP/chat/resize input."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import parse_qs
import socket,struct,subprocess,threading,time
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/tests/guild-members-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  part=c.recv(n-len(b));assert part;b+=part
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
errors=[];packets=[];posts=[];rank=0;removed=False
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_POST(self):
  try:
   fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode());posts.append(self.path)
   assert self.path=='/guild/listMembers' and fields==dict(username=['Fixture'],password=['gpu-private'],num=['50'],offset=['0'])
   time.sleep(.15)
   rows=[('Fixture',40,3210)]+([] if removed else [('Target',rank,100)])+[(f'Member{i}',0,100+i) for i in range(18)]
   body='<Members><CurrentFame>123456</CurrentFame><GuildLevel>1</GuildLevel>'+''.join(f'<Member><Name>{n}</Name><Rank>{r}</Rank><Fame>{f}</Fame></Member>' for n,r,f in rows)+'</Members>'
   data=body.encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  except BaseException as e:errors.append(e)
with ThreadingHTTPServer(('127.0.0.1',0),Handler) as http,socket.socket() as listener:
 h=threading.Thread(target=http.serve_forever,daemon=True);h.start();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(12)
 def peer():
  global rank,removed
  try:
   c,_=listener.accept()
   with c:
    assert receive(c)[0]==15
    c.sendall(frame(20,struct.pack('>ii',8,8)+utf('Guild Hall')+utf('Chronicle fixture')+struct.pack('>Ii??',1,0,True,True)))
    assert receive(c)[0]==21
    player=struct.pack('>iffB',99,3.,3.,5)+bytes([1])+struct.pack('>i',80)+bytes([0])+struct.pack('>i',80)+bytes([57])+struct.pack('>i',40)+bytes([28])+utf('Fixture')+bytes([56])+utf('Native Guild')
    sign=struct.pack('>iffB',100,3.1,3.1,0)
    update=struct.pack('>hhH',0,2,0x030e)+player+struct.pack('>H',0x01e5)+sign+struct.pack('>h',0)
    c.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
    while True:
     try:i,b=receive(c)
     except (AssertionError,ConnectionResetError):break
     packets.append((i,b));time.sleep(.2)
     def chat(name,text):c.sendall(frame(6,utf(name)+struct.pack('>iiB',99,0,0)+utf('')+utf(text)))
     if i==45:
      assert b[:8]==utf('Target');value=struct.unpack('>i',b[8:])[0]
      if value==10:rank=10;chat('@Native Guild','Target has been promoted to Member.')
      else:assert value==0;chat('*Error*','No permission')
     elif i==38:assert b==utf('Target');removed=True;chat('@Native Guild','Target has been kicked from the guild by Fixture')
  except BaseException as e:errors.append(e)
 thread=threading.Thread(target=peer,daemon=True);thread.start();cap=lambda t,n:f'{t} capture {OUT/(n+".png")}'
 script=OUT/'input.txt';script.write_text('\n'.join([
  cap(400,'panel'),'500 click 700 575',cap(550,'loading'),cap(850,'list'),
  '900 key R','925 key F','950 key 1','975 key Escape','1000 key G','1025 key Return',cap(1100,'blocked'),
  '1200 click 686 158',cap(1250,'promote'),'1350 click 325 340',cap(1450,'cancelled'),
  '1550 click 686 158','1650 click 475 340',cap(1700,'pending'),cap(2150,'promoted'),
  '2250 click 716 158',cap(2300,'demote'),'2400 click 475 340',cap(2700,'error'),'2800 click 400 330',cap(3100,'refreshed'),
  '3200 click 750 158',cap(3250,'remove'),'3350 click 475 340',cap(3800,'removed'),
  '3900 wheel -1',cap(4000,'wheel'),'4100 wheel 1','4200 down 786 130','4300 move 786 500','4350 up 786 500','4380 move 100 90',cap(4500,'bottom'),
  '4600 resize 1280 720',cap(4800,'list-1280'),'4900 resize 1600 1200',cap(5100,'list-1600'),
  '5200 resize 3840 2160',cap(5500,'list-3840'),'5600 resize 800 600',cap(5800,'restored'),
  '5900 click 400 545',cap(6100,'closed'),'6300 quit'])+'\n')
 result=subprocess.run([str(ROOT/'build/realm_client'),'--connect','127.0.0.1','--port',str(listener.getsockname()[1]),'--app-port',str(http.server_port),'--username','Fixture','--password-stdin','--character','7','--script',str(script),'--hidden','--benchmark','--frames','1000000'],input='gpu-private\n',cwd=ROOT,capture_output=True,text=True,timeout=22)
 thread.join(10);http.shutdown();assert not thread.is_alive()
 if errors:raise errors[0]
 assert result.returncode==0,result.stdout+result.stderr
 (OUT/'run.log').write_text(result.stdout+result.stderr)
assert [i for i,b in packets]==[45,45,38],packets
assert posts==['/guild/listMembers']*4,posts
assert removed and rank==10
for name,size in [('list',(800,600)),('list-1280',(1280,720)),('list-1600',(1600,1200)),('list-3840',(3840,2160))]:assert Image.open(OUT/(name+'.png')).size==size
# Chat preserves server feedback below the full-window backdrop; compare list,
# not the moving game's underlying content, for input/resize regressions.
for a,b in [('list','blocked'),('list','cancelled'),('bottom','restored')]:
 x=Image.open(OUT/(a+'.png')).convert('RGB').crop((10,100,800,504));y=Image.open(OUT/(b+'.png')).convert('RGB').crop((10,100,800,504));assert ImageChops.difference(x,y).getbbox() is None,(a,b)
for a,b in [('removed','wheel'),('removed','bottom'),('restored','closed')]:
 x=Image.open(OUT/(a+'.png')).convert('RGB').crop((10,110,800,504));y=Image.open(OUT/(b+'.png')).convert('RGB').crop((10,110,800,504));assert ImageChops.difference(x,y).getbbox(),(a,b)
print('SDL real panel/HTTP, Cancel/promote/demote rejection/remove, exact TCP requests/chat refresh, scrolling/drag, gameplay suppression and 720p/1200p/4K resize/Continue passed')
