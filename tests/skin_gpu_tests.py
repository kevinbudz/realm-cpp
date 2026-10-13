#!/usr/bin/env python3
"""Startup purchase/CREATE and actual SDL vendor chooser/RESKIN/resize/portraits."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import parse_qs
import argparse,json,socket,struct,subprocess,threading,time,tempfile
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/skin-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary-dir',type=Path,default=ROOT/'build')
args=parser.parse_args()
BINARY=args.binary_dir.resolve()/'realm_client'
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  p=c.recv(n-len(b));assert p;b+=p
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
def status(id,x,y,name,skin=0):return struct.pack('>iffB',id,x,y,7)+bytes([28])+utf(name)+bytes([7])+struct.pack('>i',20)+bytes([0])+struct.pack('>i',80)+bytes([1])+struct.pack('>i',80)+bytes([70])+struct.pack('>i',skin)+bytes([8])+struct.pack('>i',0xa97)+bytes([22])+struct.pack('>i',15)
errors=[];packets=[];posts=[];owned=[835];credits=2000
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_POST(self):
  global owned,credits
  try:
   fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode());posts.append((self.path,fields));assert self.headers['Host']==f'127.0.0.1:{self.server.server_port}'
   guest=self.path=='/char/list' and not fields
   assert guest or fields.get('username')==['SkinFixture'] and fields.get('password')==['gpu-private-fixture']
   if self.path=='/account/verify':data='<Success/>'
   elif guest:data='<Chars nextCharId="7" maxNumChars="1"><Account><Name>Guest</Name><Stats><Credits>0</Credits></Stats></Account><OwnedSkins/></Chars>'
   elif self.path=='/char/list':data=f'<Chars nextCharId="7" maxNumChars="1"><Account><Name>SkinFixture</Name><Stats><Credits>{credits}</Credits></Stats></Account><OwnedSkins>'+','.join(map(str,owned))+'</OwnedSkins></Chars>'
   elif self.path=='/account/purchaseSkin':
    assert fields['skinType'] in [['836'],['848']]
    time.sleep(.18)
    if fields['skinType']==['836']:owned.append(836);credits-=1000;data='<Success/>'
    else:data='<Error>Account in use!</Error>'
   else:raise AssertionError(self.path)
   b=data.encode();self.send_response(200);self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
  except BaseException as e:errors.append(e)
with ThreadingHTTPServer(('127.0.0.1',0),Handler) as http,socket.socket() as listener,tempfile.TemporaryDirectory(prefix='realm-skin-gpu-') as tmp:
 thread_http=threading.Thread(target=http.serve_forever,daemon=True);thread_http.start();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(45)
 def peer():
  try:
   c,_=listener.accept()
   with c:
    c.settimeout(25);assert receive(c)[0]==15
    c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Nexus')+utf('Skin fixture')+struct.pack('>Ii??',1,0,True,True)))
    i,b=receive(c);assert i==2 and b==struct.pack('>HH',782,836),(i,b);packets.append((i,b))
    vendor=struct.pack('>iffB',100,50.1,50.1,0)
    objs=[(782,status(99,50,50,'SkinFixture',836)),(0x1912,vendor),(782,status(7,51,50,'Friend',0))]
    update=struct.pack('>hh',0,len(objs))+b''.join(struct.pack('>H',t)+v for t,v in objs)+struct.pack('>h',0)
    c.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
    skin_packets=0
    while True:
     try:i,b=receive(c)
     except (AssertionError,ConnectionResetError):break
     packets.append((i,b))
     if i==47:
      skin_packets+=1;skin=struct.unpack('>i',b)[0]
      if skin_packets==3:c.sendall(frame(6,utf('*Error*')+struct.pack('>iiB',99,0,0)+utf('')+utf('Skin not owned.')))
      else:c.sendall(frame(11,struct.pack('>h',2)+status(99,50,50,'SkinFixture',skin)+status(7,51,50,'Friend',skin)+bytes([0])))
  except BaseException as e:errors.append(e)
 thread=threading.Thread(target=peer,daemon=True);thread.start()
 cap=lambda t,n:f'{t} capture {OUT/(n+".png")}'
 script=OUT/'input.txt';script.write_text('\n'.join([
  '350 click 400 540','500 click 100 130','650 click 400 130',cap(800,'startup-classic'),
  '900 click 725 200',cap(970,'purchasing'),cap(1250,'startup-owned'),
  '1350 resize 1280 720',cap(1450,'startup-1280'),'1500 resize 1600 1200',cap(1600,'startup-1600'),'1650 resize 3840 2160',cap(1800,'startup-3840'),
  '1900 resize 800 600','2050 click 400 540',cap(2350,'world-merlin'),
  '2450 click 700 575',cap(2600,'modal-merlin'),'2700 click 270 210','2800 click 510 418',cap(2950,'world-classic'),
  '3050 move 704 100',cap(3150,'group-classic'),'3250 click 704 100',cap(3350,'group-menu-classic'),
  '3450 move 100 400','3550 click 700 575','3700 click 270 280','3800 click 510 418',cap(3950,'world-merlin-again'),
  '4050 move 704 100',cap(4150,'group-merlin'),'4250 move 100 400','4350 click 700 575','4500 click 545 355',cap(4780,'purchase-error'),
  '4900 click 400 345',cap(4980,'purchase-refused'),'5050 click 700 575',
  '5150 resize 1280 720',cap(5350,'modal-1280'),'5450 resize 1600 1200',cap(5650,'modal-1600'),'5750 resize 3840 2160',cap(6050,'modal-3840'),
  '6150 resize 800 600','6350 down 230 425','6450 resize 1280 720','6550 move 100 400','6700 up 100 400',cap(6850,'resize-cancelled'),
  '6950 resize 800 600','7150 click 230 418',cap(7350,'cancelled'),
  '7450 click 700 575','7600 click 270 210','7700 click 510 418',cap(7900,'server-rejected'),
  '8000 click 700 575',cap(8200,'reopen-authoritative'),'8300 click 230 418','8450 down 100 400','8550 up 100 400','8650 quit'])+'\n')
 # SDL resize events and 4K captures need separate rendered frames. The
 # harness consumes all overdue actions in one frame, so space these fixtures.
 lines=[];delay=0;current_size=(800,600)
 for line in script.read_text().splitlines():
  stamp,rest=line.split(' ',1);stamp=int(stamp);lines.append(f'{stamp+delay} {rest}')
  if rest.startswith('resize '):
   current_size=tuple(map(int,rest.split()[1:]));delay+=1500
  elif rest.startswith('capture '):delay+=800 if current_size==(3840,2160) else 150
 script.write_text('\n'.join(lines)+'\n')
 account=Path(tmp)/'account.json';account.write_text(json.dumps(dict(username='SkinFixture',password='gpu-private-fixture')))
 r=subprocess.run([str(BINARY),'--server','127.0.0.1','--port',str(listener.getsockname()[1]),'--app-port',str(http.server_port),'--account-file',str(account),'--script',str(script),'--hidden','--benchmark','--frames','1000000'],cwd=ROOT,capture_output=True,text=True,timeout=60)
 (OUT/'run.log').write_text(r.stdout+r.stderr)
 thread.join(10);assert not thread.is_alive()
 if errors:raise errors[0]
 assert r.returncode==0,r.stdout+r.stderr
 (OUT/'run.log').write_text(r.stdout+r.stderr)
 assert [struct.unpack('>i',b)[0] for i,b in packets if i==47]==[0,836,0],[(i,b) for i,b in packets if i not in [4]]
 shots=[(i,b) for i,b in packets if i==3];assert shots,'positive firing control missing'
 assert len(shots)==1,shots
 assert len([p for p,f in posts if p=='/account/purchaseSkin'])==2,posts
 for name,size in [('startup-1280',(1280,720)),('startup-1600',(1600,1200)),('startup-3840',(3840,2160)),('modal-1280',(1280,720)),('modal-1600',(1600,1200)),('modal-3840',(3840,2160))]:assert Image.open(OUT/(name+'.png')).size==size
 assert ImageChops.difference(Image.open(OUT/'world-classic.png').convert('RGB').crop((601,195,641,233)),Image.open(OUT/'world-merlin-again.png').convert('RGB').crop((601,195,641,233))).getbbox(),'live HUD portrait unchanged'
 assert ImageChops.difference(Image.open(OUT/'group-classic.png').convert('RGB').crop((500,104,705,178)),Image.open(OUT/'group-merlin.png').convert('RGB').crop((500,104,705,178))).getbbox(),'live group portrait unchanged'
 # A guest can browse Classic and reach the selected class again after sign-in.
 guest_script=OUT/'guest.txt';guest_script.write_text('\n'.join([
  '400 click 400 540','650 click 100 130','900 click 400 130',cap(1200,'guest-skins'),
  '1400 click 400 540',cap(1600,'guest-login'),'1800 text SkinFixture','1900 key Tab','2000 text gpu-private-fixture','2100 key Return',cap(2600,'guest-resumed-skins'),'2900 quit'])+'\n')
 guest_result=subprocess.run([str(BINARY),'--server','127.0.0.1','--app-port',str(http.server_port),'--account-file',str(Path(tmp)/'guest-account.json'),'--script',str(guest_script),'--hidden','--benchmark','--frames','1000000'],cwd=ROOT,capture_output=True,text=True,timeout=12)
 assert guest_result.returncode==0,guest_result.stdout+guest_result.stderr
 (OUT/'guest.log').write_text(guest_result.stdout+guest_result.stderr)
 http.shutdown()
 if errors:raise errors[0]
 (OUT/'wire.json').write_text(json.dumps(dict(create=[782,836],reskin=[0,836,0],shots=len(shots),posts=[p for p,f in posts]),indent=2))
print('SDL startup purchase/CREATE, vendor Classic/owned RESKIN, server rejection, live portraits/group menu, modal resize/press suppression and positive firing control passed')
