#!/usr/bin/env python3
"""Optional native gameplay review: scripted input and independent social wire peer.
No AIR or live account is used. Saves frames at 800x600 and 1280x720.
"""
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import json
import argparse
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
utf=lambda value:struct.pack('>H',len(value.encode()))+value.encode()
frame=lambda ident,body:struct.pack('>IB',len(body)+5,ident)+body

def exact(conn,length):
    result=bytearray()
    while len(result)<length:
        part=conn.recv(length-len(result))
        assert part,'Unexpected disconnect'
        result.extend(part)
    return bytes(result)

def receive(conn):
    length=struct.unpack('>I',exact(conn,4))[0]
    assert 5<=length<=1048576
    data=exact(conn,length-4)
    return data[0],data[1:]

def status(oid,name='',guild='',hero=False):
    values={28:name}
    if hero:
        values.update({0:200,1:200,3:200,4:200,22:5,25:5,56:guild,57:40,91:1,59:6,60:6})
    body=struct.pack('>iffB',oid,3.,3. if hero else 3.2,len(values))
    for key,value in values.items():
        body+=bytes([key])+(utf(value) if isinstance(value,str) else struct.pack('>i',value))
    return body

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--case',choices=('create','leave','name','hall'))
parser.add_argument('--width',type=int,choices=(800,1280))
args=parser.parse_args()
output=ROOT/'build/interaction-reference'
output.mkdir(parents=True,exist_ok=True)
for width,height in [(800,600),(1280,720)]:
    if args.width and width!=args.width:continue
    for kind,object_type,guild in [('create',0x01df,''),('leave',0x01df,'Native Guild'),('name',0x01dc,''),('hall',0x072f,'Native Guild')]:
        if args.case and kind!=args.case:continue
        errors=[]
        with socket.socket() as listener,tempfile.TemporaryDirectory() as directory:
            listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
            def server():
                try:
                    conn,_=listener.accept()
                    with conn:
                        conn.settimeout(10)
                        assert receive(conn)[0]==15
                        conn.sendall(frame(20,struct.pack('>ii',8,8)+utf('Nexus')+utf('Nexus')+struct.pack('>Ii??',77,0,True,True)))
                        assert receive(conn)==(21,struct.pack('>i',7))
                        tiles=b''.join(struct.pack('>hhH',x,y,0x48) for y in range(8) for x in range(8))
                        update=struct.pack('>h',64)+tiles+struct.pack('>hH',2,0x030e)+status(99,'Original',guild,True)+struct.pack('>H',object_type)+status(77)+struct.pack('>h',0)
                        conn.sendall(frame(1,struct.pack('>ii',99,7))+frame(9,update))
                        if kind=='hall':
                            assert receive(conn)==(23,struct.pack('>i',77)),'Hall click USEPORTAL'
                        else:
                            ident=36 if kind=='create' else 38 if kind=='leave' else 49
                            value='Original' if kind=='leave' else 'Native'
                            assert receive(conn)==(ident,utf(value)),f'{kind} submit'
                            if kind=='leave':
                                body=utf('*Error*')+struct.pack('>iiB',-1,-1,0)+utf('')+utf('Guild not found.')
                                conn.sendall(frame(6,body))
                            else:
                                conn.sendall(frame(37 if kind=='create' else 50,b'\x00'+utf('Please choose another name.')))
                            assert receive(conn)==(ident,utf(value)),f'{kind} retry'
                            if kind!='leave':conn.sendall(frame(37 if kind=='create' else 50,b'\x01'+utf('')))
                            new_name='Native' if kind=='name' else 'Original'
                            new_guild='Native' if kind=='create' else ''
                            conn.sendall(frame(11,struct.pack('>h',1)+status(99,new_name,new_guild,True)+bytes([0])))
                        while conn.recv(65536):pass
                except BaseException as error:errors.append(error)
            worker=threading.Thread(target=server,daemon=True);worker.start()
            account_file=Path(directory)/'account.json'
            account_file.write_text(json.dumps({'username':'fixture','password':'test-password','charIdUseMap':{}}));account_file.chmod(0o600)
            web_requests=[]
            class App(BaseHTTPRequestHandler):
                def do_POST(self):
                    fields=urllib.parse.parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
                    web_requests.append((self.path,fields))
                    if self.path=='/account/verify':reply=b'<Success/>'
                    else:reply=b'<Chars nextCharId="8" maxNumChars="1"><Account><AccountId>1</AccountId><Name>fixture</Name><Stats><Credits>2000</Credits></Stats></Account><Char id="7"><ObjectType>0x030e</ObjectType><Level>1</Level></Char></Chars>'
                    self.send_response(200);self.end_headers();self.wfile.write(reply)
                def log_message(self,*args):pass
            app=ThreadingHTTPServer(('127.0.0.1',0),App)
            app_worker=threading.Thread(target=app.serve_forever,daemon=True);app_worker.start()
            sx=round(width-100*height/600);sy=round(575*height/600)
            capture=lambda label:f'{output}/{kind}-{width}-{label}.png'
            lines=[f'300 capture {capture("panel")}',f'400 click {sx} {sy}']
            if kind!='hall':
                if kind!='leave':lines+=['500 text Native']
                lines+=[f'650 capture {capture("dialog")}', '700 key Return',f'900 capture {capture("error")}', '1000 key Return',f'1200 capture {capture("accepted")}']
            if kind=='name':
                lines=[str(int(line.split(' ',1)[0])+600)+' '+line.split(' ',1)[1] for line in lines]
                lines=[f'300 click {width//2} {round(535*height/600)}',f'500 click {round((width-800*height/600)/2+100*height/600)} {round(140*height/600)}']+lines
                lines+=['1900 key Escape','2300 quit']
            else:lines+=['1400 quit']
            script=Path(directory)/'script.txt';script.write_text('\n'.join(lines)+'\n')
            launch=[str(ROOT/'build/realm_client'),'--server','localhost','--app-port',str(app.server_port),'--account-file',str(account_file)] if kind=='name' else [str(ROOT/'build/realm_viewer'),'--connect','localhost','--username','fixture','--character','7']
            process=subprocess.Popen(launch+['--port',str(listener.getsockname()[1]),'--fonts',str(ROOT/'build/fonts-client'),'--hud-art',str(ROOT/'build/hud-art'),'--hidden','--benchmark','--frames','100000','--width',str(width),'--height',str(height),'--script',str(script)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,cwd=ROOT)
            peak=[0];finished=threading.Event()
            def memory_monitor():
                while not finished.is_set():
                    try:
                        for line in Path(f'/proc/{process.pid}/status').read_text().splitlines():
                            if line.startswith('VmHWM:'):peak[0]=max(peak[0],int(line.split()[1]))
                    except FileNotFoundError:pass
                    finished.wait(.01)
            monitor=threading.Thread(target=memory_monitor,daemon=True);monitor.start()
            try:stdout,stderr=process.communicate(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill();process.communicate();raise
            finally:finished.set();monitor.join(1)
            result=subprocess.CompletedProcess(process.args,process.returncode,stdout,stderr)
            if peak[0]:(output/f'{kind}-{width}-memory.txt').write_text(f'Peak resident memory sampled from Linux VmHWM: {peak[0]} KiB\n')
            app.shutdown();app.server_close();app_worker.join(2)
            worker.join(5)
            assert not worker.is_alive(),'Wire peer did not finish'
            assert result.returncode==0,result.stdout+result.stderr
            if errors:raise errors[0]
            for label in ('panel',) if kind=='hall' else ('panel','dialog','error','accepted'):
                image=Image.open(capture(label));assert image.size==(width,height),'Screenshot dimensions'
            if kind=='name':
                saved=json.loads(account_file.read_text());assert saved['username']=='Native' and saved['password']=='test-password','Saved credentials after rename'
                assert any(path=='/char/list' and fields.get('username')==['Native'] for path,fields in web_requests),'Return to character selection uses renamed credentials'
                assert account_file.stat().st_mode&0o077==0,'Saved credentials remain private'
            (output/f'{kind}-{width}-timings.txt').write_text(result.stdout)
            print(f'{kind} {width}x{height}: input, rejection/retry and native captures passed')
