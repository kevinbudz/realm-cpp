#!/usr/bin/env python3
"""Independent wire peer: map guard, strict cooldown and current eligibility."""
from pathlib import Path
import socket,struct,subprocess,sys,threading
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode();frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
 b=b''
 while len(b)<n:
  part=c.recv(n-len(b));assert part;b+=part
 return b
def receive(c):
 n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
def status(id,name,condition):
 return struct.pack('>iffB',id,3.,3.,2)+bytes([28])+utf(name)+bytes([26])+struct.pack('>i',condition)
for mode in ['allowed','forbidden']:
 errors=[];packets=[]
 with socket.socket() as listener:
  listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
  def peer():
   try:
    c,_=listener.accept()
    with c:
     c.settimeout(14);assert receive(c)[0]==15
     c.sendall(frame(20,struct.pack('>ii',8,8)+utf('Nexus')+utf('Nexus')+struct.pack('>Ii??',1,0,True,mode=='allowed')))
     assert receive(c)[0]==21
     update=struct.pack('>hh',0,3)+b''.join(struct.pack('>H',775)+status(id,name,cond) for id,name,cond in [(99,'Fixture',0),(7,'Newer',2048),(2,'Older',0)])+struct.pack('>h',0)
     c.sendall(frame(1,struct.pack('>ii',99,0))+frame(9,update))
     while True:
      try:i,b=receive(c)
      except (AssertionError,ConnectionResetError):break
      packets.append((i,b))
      if i==22:
       if struct.unpack('>i',b)[0]==2:c.sendall(frame(11,struct.pack('>h',1)+status(7,'Newer',0)+bytes([0])))
       c.sendall(frame(16,struct.pack('>iff',99,3.,3.)))
   except BaseException as e:errors.append(e)
  worker=threading.Thread(target=peer,daemon=True);worker.start()
  result=subprocess.run([str(Path(sys.argv[1]).resolve()),mode,str(listener.getsockname()[1])],input='private-fixture\n',capture_output=True,text=True,timeout=17)
  worker.join(5);assert not worker.is_alive()
  if errors:raise errors[0]
  assert result.returncode==0,result.stdout+result.stderr
 assert [(i,b) for i,b in packets if i not in [4,48]]==([(22,struct.pack('>i',2)),(22,struct.pack('>i',7))] if mode=='allowed' else []),packets
 print(result.stdout.strip())
