#!/usr/bin/env python3
"""Board panel click, SDL editor/modal/resize and native HTTP/GPU integration."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socket
import struct
import subprocess
import threading
import time
from urllib.parse import parse_qs,unquote_plus
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/guild-board-reference/gpu';OUT.mkdir(parents=True,exist_ok=True)
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode()
frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b
def exact(c,n):
    b=b''
    while len(b)<n:
        part=c.recv(n-len(b));assert part;b+=part
    return b
def receive(c):
    n=struct.unpack('>I',exact(c,4))[0];b=exact(c,n-4);return b[0],b[1:]
errors=[];packets=[];posts=[];board='Welcome to the guild!\nhttps://example.com/guild'
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*a):pass
    def do_POST(self):
        global board
        fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(),keep_blank_values=True);posts.append(self.path)
        assert fields['username']==['Fixture'] and fields['password']==['gpu-private']
        time.sleep(.25)
        if self.path=='/guild/setBoard':board=unquote_plus(fields['board'][0])
        else:assert self.path=='/guild/getBoard'
        data=board.encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
with ThreadingHTTPServer(('127.0.0.1',0),Handler) as http, socket.socket() as listener:
    h=threading.Thread(target=http.serve_forever,daemon=True);h.start()
    listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
    def peer():
        try:
            c,_=listener.accept()
            with c:
                assert receive(c)[0]==15
                c.sendall(frame(20,struct.pack('>ii',8,8)+utf('Guild Hall')+utf('Board fixture')+struct.pack('>Ii??',1,0,True,True)))
                assert receive(c)[0]==21
                player=struct.pack('>iffB',99,3.,3.,3)+bytes([1])+struct.pack('>i',80)+bytes([0])+struct.pack('>i',80)+bytes([57])+struct.pack('>i',20)
                sign=struct.pack('>iffB',100,3.1,3.1,0)
                update=struct.pack('>hhH',0,2,0x030e)+player+struct.pack('>H',0x01e7)+sign+struct.pack('>h',0)
                c.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
                while True:
                    try:i,b=receive(c)
                    except (AssertionError,ConnectionResetError):break
                    packets.append(i)
        except BaseException as e:errors.append(e)
    thread=threading.Thread(target=peer,daemon=True);thread.start()
    cap=lambda t,n:f'{t} capture {OUT/(n+".png")}'
    script=OUT/'input.txt';script.write_text('\n'.join([
        cap(400,'panel'),'500 click 700 575',cap(600,'loading'),cap(900,'view'),
        '1000 key R','1050 key F','1100 key 1','1150 key Escape','1200 key G',cap(1300,'blocked'),
        '1400 click 230 500',cap(1500,'edit'),'1600 click 210 90','1625 key Home','1650 text Added + %20 & café.','1700 key Return',cap(1800,'typed'),
        '1900 click 550 500',cap(2000,'saving'),cap(2300,'saved'),
        '2400 resize 1280 720',cap(2600,'view-1280'),'2700 resize 1600 1200',cap(2900,'view-1600'),
        '3000 resize 3840 2160',cap(3300,'view-3840'),'3400 resize 800 600',cap(3600,'restored'),
        '3700 click 550 500',cap(3900,'closed'),'4000 quit'])+'\n')
    result=subprocess.run([str(ROOT/'build/realm_viewer'),'--connect','localhost','--port',str(listener.getsockname()[1]),'--app-port',str(http.server_port),'--username','Fixture','--password-stdin','--character','7','--fonts',str(ROOT/'build/fonts-client'),'--hud-art',str(ROOT/'build/hud-art'),'--script',str(script),'--hidden','--benchmark','--frames','100000'],input='gpu-private\n',cwd=ROOT,capture_output=True,text=True,timeout=20)
    thread.join(10);http.shutdown();assert not thread.is_alive()
    if errors:raise errors[0]
    assert result.returncode==0,result.stdout+result.stderr
    (OUT/'run.log').write_text(result.stdout+result.stderr)
assert posts==['/guild/getBoard','/guild/setBoard'],posts
assert board.startswith('Added + %20 & café.\nWelcome'),repr(board)
assert not packets,f'Modal input leaked to gameplay: {packets}'
for name,size in [('view',(800,600)),('view-1280',(1280,720)),('view-1600',(1600,1200)),('view-3840',(3840,2160))]:
    im=Image.open(OUT/(name+'.png')).convert('RGB');assert im.size==size
    s=size[1]/600;x=(size[0]-400*s)/2;y=(size[1]-432*s)/2
    assert im.getpixel((int(x+100*s),int(y+200*s)))==(51,51,51),(name,im.getpixel((int(x+100*s),int(y+200*s))))
box=(194,78,606,522)
a=Image.open(OUT/'view.png').convert('RGB').crop(box);b=Image.open(OUT/'blocked.png').convert('RGB').crop(box)
assert ImageChops.difference(a,b).getbbox() is None,'Gameplay keys changed modal'
a=Image.open(OUT/'saved.png').convert('RGB').crop(box);b=Image.open(OUT/'restored.png').convert('RGB').crop(box)
assert ImageChops.difference(a,b).getbbox() is None,'Resize restores base board'
assert ImageChops.difference(b,Image.open(OUT/'closed.png').convert('RGB').crop(box)).getbbox(),'Close removes board'
print('Native TCP panel click, SDL multiline input, HTTP load/save, modal gameplay suppression, 720p/1200p/4K resize and Close passed')
