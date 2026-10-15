#!/usr/bin/env python3
"""Native SDL holds, edge hover, menu, live teleport eligibility and GPU resize."""
from pathlib import Path
import socket,struct,subprocess,threading,time,json
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/tests/party-arrow-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  x=c.recv(n-len(b));assert x;b+=x
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
def status(id,x,y,name,condition=0):
 stats=bytes([28])+utf(name)+bytes([7])+struct.pack('>i',20)+bytes([26])+struct.pack('>i',condition)
 if id==99:stats+=bytes([8])+struct.pack('>i',0xa04)
 return struct.pack('>iffB',id,x,y,4 if id==99 else 3)+stats
for mode in ['allowed','forbidden','invisible']:
 errors=[];packets=[];allowed=mode!='forbidden'
 with socket.socket() as listener:
  listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(12)
  def peer():
   try:
    c,_=listener.accept()
    with c:
     c.settimeout(22);assert receive(c)[0]==15
     c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Nexus')+utf('Party arrows')+struct.pack('>Ii??',1,0,True,allowed)));assert receive(c)[0]==21
     objs=[(0x030e,status(99,50,50,'Local')),(782,status(7,78.28427,21.71573,'Tank',2048)),(775,status(2,78.28427,21.61573,'Wizzy',2048 if mode=='invisible' else 0)),(782,status(8,21.71573,21.71573,'North'))]
     update=struct.pack('>hh',0,len(objs))+b''.join(struct.pack('>H',t)+b for t,b in objs)+struct.pack('>h',0)
     c.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
     start=time.monotonic()
     while True:
      try:i,b=receive(c)
      except (AssertionError,ConnectionResetError):break
      packets.append((i,b,time.monotonic()-start))
      if i==22 and len([p for p in packets if p[0]==22])==1:c.sendall(frame(11,struct.pack('>h',2)+status(7,78.28427,21.71573,'Tank',0)+status(2,78.28427,21.61573,'Wizzy',2048)+bytes([0])))
   except BaseException as e:errors.append(e)
  t=threading.Thread(target=peer,daemon=True);t.start();cap=lambda t,n:f'{t} capture {OUT/(mode+"-"+n+".png")}'
  actions=[cap(350,'arrows'),'400 move 590 325',cap(550,'tooltip'),'650 down 590 325',cap(800,'menu-held'),
   '850 move 200 400',cap(1000,'rollout-held'),'1100 up 200 400','1200 move 590 325','1300 click 590 325',cap(1450,'menu'),
   '1500 move 465 195',cap(1600,'hover'),'1700 click 465 195',cap(1850,'result')]
  if mode=='allowed':
   actions+=['1950 move 590 325','2050 click 590 325','2150 move 465 195',cap(2300,'cooldown'),'2400 click 465 195',cap(2550,'refusal'),
    '2700 move 590 325','2800 click 590 325','2900 move 465 195',cap(6500,'mid'),'11900 click 465 195',cap(12100,'expiry'),
    '12200 key C','12300 move 590 450',cap(12500,'offset'),'12600 key C',
    '12700 resize 1280 720','12800 move 1030 390',cap(13000,'tooltip-1280'),
    '13100 click 1030 390','13200 move 865 224',cap(13400,'menu-1280'),
    '13500 resize 1600 1200','13600 move 1190 650',cap(13800,'tooltip-1600'),
    '13900 click 1190 650','14000 move 920 374',cap(14200,'menu-1600'),
    '14300 resize 3840 2160','14400 move 3110 1170',cap(14600,'tooltip-3840'),
    '14700 click 3110 1170','14800 move 2630 674',cap(15000,'menu-3840'),
    '15100 resize 800 600','15200 move 590 325','15300 down 590 325','15400 resize 1280 720','15500 move 200 400',cap(15600,'resize-held'),'15700 up 200 400','15800 resize 800 600','15900 move 100 400',cap(16000,'closed'),
    # Positive control proves the same SDL hold reaches Session::shoot outside UI.
    '16100 down 200 400','16300 up 200 400','16600 quit']
  else:actions+=['2000 move 100 400',cap(2200,'closed'),'2400 quit']
  script=OUT/(mode+'.txt');script.write_text('\n'.join(actions)+'\n')
  r=subprocess.run([str(ROOT/'build/realm_client'),'--connect','127.0.0.1','--port',str(listener.getsockname()[1]),'--username','Fixture','--character','7','--script',str(script),'--hidden','--benchmark','--frames','1000000'],cwd=ROOT,capture_output=True,text=True,timeout=28)
  t.join(10);assert not t.is_alive()
  if errors:raise errors[0]
  assert r.returncode==0,r.stdout+r.stderr;(OUT/(mode+'.log')).write_text(r.stdout+r.stderr)
 teleports=[b for i,b,_ in packets if i==22];assert teleports==([struct.pack('>i',2),struct.pack('>i',7)] if mode=='allowed' else []),[(i,b) for i,b,_ in packets]
 shots=[t for i,b,t in packets if i==3];assert all(t>16 for t in shots),shots
 if mode=='allowed':assert shots,'Positive firing control did not send PLAYERSHOOT'
 assert all(i in [4,3,22] for i,b,t in packets),[(i,b) for i,b,t in packets]
 (OUT/(mode+'-wire.json')).write_text(json.dumps(dict(teleports=[struct.unpack('>i',b)[0] for b in teleports],shot_times=shots,packet_ids=[i for i,b,t in packets]),indent=2))
 menu=Image.open(OUT/(mode+'-menu.png')).convert('RGB');closed=Image.open(OUT/(mode+'-closed.png')).convert('RGB');assert menu.getpixel((550,305))==(54,54,54),mode
 assert ImageChops.difference(menu.crop((428,176,584,318)),closed.crop((428,176,584,318))).getbbox(),mode
 if mode=='allowed':
  for n,size in [('tooltip-1280',(1280,720)),('menu-1280',(1280,720)),('tooltip-1600',(1600,1200)),('menu-1600',(1600,1200)),('tooltip-3840',(3840,2160)),('menu-3840',(3840,2160))]:assert Image.open(OUT/(mode+'-'+n+'.png')).size==size
print('SDL party edge/group hover, held-click and rollout suppression (positive firing control), menu/offset/resize, live eligibility/cooldown/map flag checks passed')
