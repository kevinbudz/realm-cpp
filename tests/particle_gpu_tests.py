#!/usr/bin/env python3
"""Actual SDL/TCP/OpenGL effect packets, ability/shot input, density, resize, teardown."""
from pathlib import Path
import socket,struct,subprocess,threading,time,json,re
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/tests/particle-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  p=c.recv(n-len(b));assert p;b+=p
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
def status(id,x,y,name):
 values=[(28,utf(name)),(7,struct.pack('>i',20)),(0,struct.pack('>i',200)),(1,struct.pack('>i',200)),(8,struct.pack('>i',0xa6e)),(9,struct.pack('>i',0xaa3)),(3,struct.pack('>i',200)),(4,struct.pack('>i',200)),(26,struct.pack('>i',0))]
 return struct.pack('>iffB',id,x,y,len(values))+b''.join(bytes([t])+v for t,v in values)
def effect(kind,target=99,color=0xff33ccff,a=(52,50),b=None):
 return frame(14,struct.pack('>BiIff',kind,target,color,*a)+(struct.pack('>ff',*b) if b is not None else b''))
def update(objects):
 tiles=[(x,y,0x1) for y in range(40,61) for x in range(40,61)]
 return frame(9,struct.pack('>h',len(tiles))+b''.join(struct.pack('>hhH',*t) for t in tiles)+struct.pack('>h',len(objects))+b''.join(struct.pack('>H',t)+status(*s) for t,s in objects)+struct.pack('>h',0))
errors=[];packets=[];stop=threading.Event()
with socket.socket() as listener:
 listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(20)
 def peer():
  try:
   c,_=listener.accept()
   with c:
    c.settimeout(20);assert receive(c)[0]==15;c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Nexus')+utf('Particle fixture')+struct.pack('>Ii??',1,0,True,True)));assert receive(c)[0]==21
    objs=[(782,(99,50,50,'Local')),(782,(7,50,49,'Friend')),(0x1c7,(8,48,50,'Fountain')),(0x731,(9,52,51,'Vent')),(0x1e15,(10,51,50,'Custom'))]
    c.sendall(frame(1,struct.pack('>ii',99,7))+update(objs));start=time.monotonic()
    def events():
     try:
      while not stop.wait(.09):
       elapsed=time.monotonic()-start
       if elapsed<.7:continue
       if elapsed<3.8:
        c.sendall(effect(1)+effect(5,a=(3,0))+effect(7,target=7,a=(52,50))+effect(11,a=(48,50),b=(200,0))+effect(4,a=(52,50),b=(800,0))+effect(15,color=0xffff0000,a=(.4,20)))
       elif elapsed<5.6:
        c.sendall(b''.join(effect(6,color=0xff33ccff,a=(0,0)) for _ in range(500)))
       else:
        c.sendall(frame(20,struct.pack('>ii',100,100)+utf('Vault')+utf('Particle teardown')+struct.pack('>Ii??',1,0,True,False))+frame(1,struct.pack('>ii',99,7))+update([(782,(99,50,50,'Local'))]));return
     except (OSError,AssertionError):pass
    changer=threading.Thread(target=events,daemon=True);changer.start()
    while True:
     try:i,b=receive(c)
     except (AssertionError,ConnectionResetError):break
     packets.append((i,b))
     if i==13:c.sendall(effect(4,color=0xffddff00,a=(52,50),b=(800,0)))
     if i==3:c.sendall(frame(7,struct.pack('>iihffffBh',123,99,0xa61,50,50,0,0,1,10)))
    stop.set();changer.join(1)
  except BaseException as e:errors.append(e)
 t=threading.Thread(target=peer,daemon=True);t.start();cap=lambda t,n:f'{t} capture {OUT/(n+".png")}'
 script=OUT/'script.txt';script.write_text('\n'.join([cap(450,'emitters'),cap(1400,'effects'),'1600 move 400 325','1600 ability-down','1610 ability-up',cap(1800,'ability'),'1900 resize 1600 1200',cap(2500,'resized'),'3000 resize 800 600','3200 down 200 400','3500 up 200 400',cap(3540,'trail'),cap(4600,'dense'),cap(6800,'teardown'),'7200 quit'])+'\n')
 r=subprocess.run([str(ROOT/'build/realm_client'),'--connect','127.0.0.1','--port',str(listener.getsockname()[1]),'--username','Fixture','--character','7','--script',str(script),'--hidden','--benchmark','--frames','1000000'],cwd=ROOT,capture_output=True,text=True,timeout=22)
 t.join(10);assert not t.is_alive();assert not errors,errors;assert r.returncode==0,r.stdout+r.stderr;(OUT/'run.log').write_text(r.stdout+r.stderr)
 rows={name:tuple(map(int,values)) for name,*values in re.findall(r'Particle capture "(.*?)\.png" particles=(\d+) emitters=(\d+) flashes=(\d+) dropped=(\d+) quads=(\d+)',r.stdout)}
 assert rows['emitters'][0]>0 and rows['emitters'][1]==3,rows
 assert rows['effects'][0]>50 and rows['effects'][2]>0,rows
 assert 1000<rows['dense'][0]<=8192 and rows['dense'][3]>0,rows
 assert rows['teardown'][:3]==(0,0,0),rows
 assert sum(i==13 for i,b in packets)==1,packets
 assert any(i==3 for i,b in packets),'actual held-shot input absent'
 assert any(i==31 for i,b in packets),'server projectile acknowledgment absent'
 trail_image=Image.open(OUT/'trail.png').convert('RGB');assert sum(1 for rr,g,b in trail_image.crop((100,100,590,500)).getdata() if rr>180 and b>180 and g<80)>8,'projectile trail missing'
 assert Image.open(OUT/'resized.png').size==(1600,1200)
 effect_image=Image.open(OUT/'effects.png').convert('RGB');assert sum(1 for rr,g,b in effect_image.crop((100,100,550,500)).getdata() if b>rr*1.2 and g>rr*1.2)>50,'colored ability cues missing'
 (OUT/'wire.json').write_text(json.dumps(dict(ids=[i for i,b in packets],captures=rows),indent=2))
print('Actual SDL/TCP/OpenGL SHOWEFFECTs, heal/nova/line/lightning/custom throw/flash, source emitters, ability and held shot, resize, dense budget and map teardown passed')
