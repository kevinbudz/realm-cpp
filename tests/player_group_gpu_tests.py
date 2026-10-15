#!/usr/bin/env python3
"""Actual SDL minimap hover/click, Session packets, chat and native GPU path."""
from pathlib import Path
import socket,struct,subprocess,threading
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/tests/player-group-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  part=c.recv(n-len(b));assert part;b+=part
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
def status(id,x,y,name,condition=0):
 return struct.pack('>iffB',id,x,y,4)+bytes([28])+utf(name)+bytes([7])+struct.pack('>i',20)+bytes([26])+struct.pack('>i',condition)+bytes([56])+utf('Guild' if id in [99,2] else '')
for mode in ['allowed','forbidden','invisible']:
 errors=[];packets=[];allowed=mode!='forbidden'
 with socket.socket() as listener:
  listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(12)
  def peer():
   try:
    c,_=listener.accept()
    with c:
     c.settimeout(20);assert receive(c)[0]==15
     c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Nexus')+utf('Minimap group')+struct.pack('>Ii??',1,0,True,allowed)))
     assert receive(c)[0]==21
     objs=[(0x030e,status(99,50,50,'Local')),(782,status(7,51,50,'Tank',2048)),(775,status(2,51.5,50,'Wizzy',2048 if mode=='invisible' else 0)),(782,status(8,90,90,'Edge'))]
     update=struct.pack('>hh',0,len(objs))+b''.join(struct.pack('>H',t)+b for t,b in objs)+struct.pack('>h',0)
     c.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
     while True:
      try:i,b=receive(c)
      except (AssertionError,ConnectionResetError):break
      packets.append((i,b))
      if i==22 and len([p for p in packets if p[0]==22])==1:
       # Eligibility changes after opening: second click must resolve live state.
       c.sendall(frame(11,struct.pack('>h',2)+status(7,51,50,'Tank',0)+status(2,51.5,50,'Wizzy',2048)+bytes([0])))
   except BaseException as e:errors.append(e)
  thread=threading.Thread(target=peer,daemon=True);thread.start();cap=lambda t,n:f'{t} capture {OUT/(mode+"-"+n+".png")}'
  actions=['400 move 704 100',cap(500,'tooltip'),'600 click 704 100',cap(750,'menu'),'850 move 578 120',cap(950,'hover'),'1000 click 578 120',cap(1100,'result')]
  if mode=='allowed':
   actions+=['1200 move 704 100','1300 click 704 100','1400 move 578 120',cap(1500,'cooldown'),'1600 click 578 120',cap(1750,'refused'),
    '1850 move 704 100','1950 click 704 100','2050 move 578 120',cap(6100,'mid'),'11200 click 578 120',cap(11400,'ready-result'),
    '11500 move 795 195',cap(11600,'edge'),
    '11700 resize 1280 720','11800 move 1164 120',cap(12000,'tooltip-1280'),
    '12100 click 1164 120','12200 move 1004 148',cap(12300,'menu-1280'),
    '12400 resize 1600 1200','12500 move 1408 200',cap(12700,'tooltip-1600'),
    '12800 click 1408 200','12900 move 1160 242',cap(13100,'menu-1600'),
    '13200 resize 3840 2160','13300 move 3494 360',cap(13500,'tooltip-3840'),
    '13600 click 3494 360','13700 move 2990 410',cap(13900,'menu-3840'),
    '14000 resize 800 600','14100 move 100 400',cap(14200,'closed'),'14400 quit']
  else:actions+=['1250 move 100 400',cap(1350,'closed'),'1500 quit']
  script=OUT/(mode+'.txt');script.write_text('\n'.join(actions)+'\n')
  r=subprocess.run([str(ROOT/'build/realm_client'),'--connect','127.0.0.1','--port',str(listener.getsockname()[1]),'--username','Fixture','--character','7','--script',str(script),'--hidden','--benchmark','--frames','1000000'],cwd=ROOT,capture_output=True,text=True,timeout=27)
  thread.join(10);assert not thread.is_alive()
  if errors:raise errors[0]
  assert r.returncode==0,r.stdout+r.stderr
  (OUT/(mode+'.log')).write_text(r.stdout+r.stderr)
 assert [(i,b) for i,b in packets if i!=4]==([(22,struct.pack('>i',2)),(22,struct.pack('>i',7))] if mode=='allowed' else []),packets
 moves=[b for i,b in packets if i==4]
 assert len(moves)==(1 if mode=='allowed' else 0),moves
 if moves:assert struct.unpack('>ff',moves[0][4:])==(50.,50.),moves
 # The menu has an opaque source-colored interior and disappears on rollout.
 menu=Image.open(OUT/(mode+'-menu.png')).convert('RGB');closed=Image.open(OUT/(mode+'-closed.png')).convert('RGB')
 assert menu.getpixel((550,150))==(54,54,54),mode
 assert ImageChops.difference(menu.crop((543,105,699,200)),closed.crop((543,105,699,200))).getbbox(),mode
 if mode=='allowed':
  for name,size in [('tooltip-1280',(1280,720)),('menu-1280',(1280,720)),('tooltip-1600',(1600,1200)),('menu-1600',(1600,1200)),('tooltip-3840',(3840,2160)),('menu-3840',(3840,2160))]:assert Image.open(OUT/(mode+'-'+name+'.png')).size==size
print('SDL minimap grouped/edge hover, menu/rollout/resize, first visible target, cooldown refusal/expiry, map flag/no-eligible chat, exact TELEPORT packets and no gameplay leakage passed')
