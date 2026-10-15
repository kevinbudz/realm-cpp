#!/usr/bin/env python3
"""Actual SDL/TCP/OpenGL live dyes, portraits, sizes and cache churn."""
from pathlib import Path
import socket,struct,subprocess,threading,time,json,re
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/tests/dye-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  p=c.recv(n-len(b));assert p;b+=p
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
def status(id,x,y,name,tex1=0,tex2=0,size=100,skin=0,items=False):
 stats=[(28,utf(name)),(7,struct.pack('>i',20)),(0,struct.pack('>i',80)),(1,struct.pack('>i',80)),(29,struct.pack('>i',tex1)),(30,struct.pack('>i',tex2)),(2,struct.pack('>i',size)),(70,struct.pack('>i',skin)),(22,struct.pack('>i',15)),(8,struct.pack('>i',0xa97))]
 if items:stats.append((12,struct.pack('>i',0x1000)))
 return struct.pack('>iffB',id,x,y,len(stats))+b''.join(bytes([t])+v for t,v in stats)
def tick(tex1,tex2,size=137,skin=0):
 return frame(11,struct.pack('>h',3)+status(99,50,50,'Local',tex1,tex2,size,skin)+status(7,51,50,'Friend',tex1,tex2,350,skin)+status(8,49,50,'Dye bottle',tex1,0,73)+bytes([0]))
errors=[];packets=[]
with socket.socket() as listener:
 listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(25)
 def peer():
  try:
   c,_=listener.accept()
   with c:
    c.settimeout(30);assert receive(c)[0]==15;c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Nexus')+utf('Dye fixture')+struct.pack('>Ii??',1,0,True,True)));assert receive(c)[0]==21
    objs=[(782,status(99,50,50,'Local',size=137,items=True)),(782,status(7,51,50,'Friend',size=350)),(0x1000,status(8,49,50,'Dye bottle',size=73)),(0xa97,status(9,50,51,'Unbaked object',size=83))]
    c.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,struct.pack('>hh',0,len(objs))+b''.join(struct.pack('>H',t)+v for t,v in objs)+struct.pack('>h',0)))
    stop=threading.Event()
    def changes():
     try:
      if stop.wait(3):return
      c.sendall(tick(0x1ff0000,0x10000ff));
      if stop.wait(2):return
      c.sendall(tick(0x4000007,0x5000008,173,836));
      if stop.wait(7):return
      for i in range(320):
       if stop.wait(.012):return
       c.sendall(tick(0x1000000|((i*4567)&0xffffff),0x9000007,137,836))
      c.sendall(tick(0x1ff0000,0x10000ff,137,836))
      if stop.wait(1):return
      dense=[(782,status(1000+i,48+(i%20)*.2,48+(i//20)*.2,'Churn',0x1000000|((i*4567)&0xffffff),0x5000008,25)) for i in range(300)]
      c.sendall(frame(9,struct.pack('>hh',0,len(dense))+b''.join(struct.pack('>H',t)+v for t,v in dense)+struct.pack('>h',0)))
      if stop.wait(4):return
      c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Vault')+utf('Dye reset')+struct.pack('>Ii??',1,0,True,False))+frame(1,struct.pack('>ii',99,7))+frame(9,struct.pack('>hh',0,len(objs))+b''.join(struct.pack('>H',t)+v for t,v in objs)+struct.pack('>h',0)))
     except (OSError,AssertionError):pass
    changer=threading.Thread(target=changes,daemon=True);changer.start()
    while True:
     try:i,b=receive(c)
     except (AssertionError,ConnectionResetError):break
     packets.append((i,b))
    stop.set();changer.join(1)
  except BaseException as e:errors.append(e)
 thread=threading.Thread(target=peer,daemon=True);thread.start();cap=lambda t,n:f'{t} capture {OUT/(n+".png")}'
 actions=[cap(400,'neutral-world'),'500 key 1','700 move 704 100',cap(850,'neutral-group'),'1000 click 704 100',cap(1200,'neutral-menu'),cap(3600,'red-menu'),
 '4000 move 100 400',cap(4300,'red-world'),'5400 move 704 100',cap(5700,'textile-group'),'6000 resize 1280 720','7800 move 1164 120',cap(8200,'textile-1280'),
 '8500 resize 1600 1200','10300 move 1408 200',cap(10800,'textile-1600'),'11200 resize 800 600',cap(16500,'churn-world'),
 '16900 resize 3840 2160','18900 move 3494 360',cap(19900,'red-3840'),'21000 resize 800 600','22700 move 100 400','23000 down 100 400','23100 up 100 400',cap(23400,'final-world'),'23800 quit']
 script=OUT/'input.txt';script.write_text('\n'.join(actions)+'\n')
 r=subprocess.run([str(ROOT/'build/realm_client'),'--connect','127.0.0.1','--port',str(listener.getsockname()[1]),'--username','Fixture','--character','7','--script',str(script),'--hidden','--benchmark','--frames','1000000'],cwd=ROOT,capture_output=True,text=True,timeout=40)
 thread.join(5);assert not thread.is_alive()
 if errors:raise errors[0]
 assert r.returncode==0,r.stdout+r.stderr
 (OUT/'run.log').write_text(r.stdout+r.stderr)
 for first,second,region in [('neutral-world','red-world',(600,190,645,235)),('neutral-menu','red-menu',(510,105,710,200))]:
  assert ImageChops.difference(Image.open(OUT/(first+'.png')).convert('RGB').crop(region),Image.open(OUT/(second+'.png')).convert('RGB').crop(region)).getbbox(),(first,second)
 for name,size in [('textile-1280',(1280,720)),('textile-1600',(1600,1200)),('red-3840',(3840,2160))]:assert Image.open(OUT/(name+'.png')).size==size
 assert len([i for i,b in packets if i==13])==1,packets
 assert len([i for i,b in packets if i==3])==1,'UI leakage/positive shot control'
 counts=re.search(r'Appearance CPU=(\d+)/(\d+) GPU=(\d+)/(\d+) evictions=(\d+)',r.stdout);assert counts,r.stdout
 cpu,cbytes,gpu,gbytes,evictions=map(int,counts.groups());assert cpu<=10 and gpu<=10 and cbytes<=32*1024*1024 and gbytes<=32*1024*1024 and evictions>0,counts.groups()
 (OUT/'wire.json').write_text(json.dumps(dict(useitem=1,shots=1,cache=list(map(int,counts.groups())),live_dye_changes=322,same_frame_appearances=300,map_teardown=True),indent=2))
print('SDL USEITEM, live world/HUD/group menu dyes/skins/sizes, unbaked appearances, resize, cache churn, 300 concurrent appearances, map teardown and positive shot passed')
