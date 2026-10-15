#!/usr/bin/env python3
"""Legends screen: production SDL/HTTP routes with disposable accounts; no live backend."""
import argparse
import json
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
from PIL import Image, ImageChops
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary-dir',type=Path,default=ROOT/'build/tests')
p.add_argument('--output',type=Path,default=ROOT/'build/tests/legends-gpu')
p.add_argument('--visible',action='store_true')
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
results={}
for scenario in ('success','error','late','detail-late','teardown'):
 posts=[];errors=[];held=threading.Event();detail_release=threading.Event();stop=threading.Event()
 detail_pending=a.output/f'{scenario}-detail-pending.png';detail_pending.unlink(missing_ok=True)
 pending=a.output/f'{scenario}-pending.png';pending.unlink(missing_ok=True)
 class App(BaseHTTPRequestHandler):
  def do_POST(self):
   fields=parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(),keep_blank_values=True)
   posts.append((self.path,fields))
   if self.path=='/account/verify':reply=b'<Success/>'
   elif self.path=='/char/list':reply=b'<Chars nextCharId="9" maxNumChars="3"><Account><AccountId>42</AccountId><Name>Fixture</Name></Account></Chars>'
   elif self.path=='/fame/list':
    span=fields.get('timespan',[''])[0]
    if fields!={'timespan':[span],'accountId':['42'],'charId':['-1']} or span not in ('week','month','all'):errors.append(str(fields))
    count=sum(path=='/fame/list' and f['timespan']==[span] for path,f in posts)
    if span=='week' and count==1:
     if not held.wait(60):errors.append('held response capture missing')
    if scenario=='error':reply=b'<Error>Fixture Legends unavailable</Error>'
    elif span=='month':reply=b'<FameList timespan="month"/>'
    elif span=='all' and count==1:reply=b'<FameList timespan="week"/>'
    else:reply=(ROOT/'tests/fixtures/legends.xml').read_bytes().replace(b'timespan="week"',f'timespan="{span}"'.encode())
   elif self.path=='/char/fame':
    expected=({'accountId':['90'],'charId':['100']} if scenario=='detail-late' and len([p for p,f in posts if p=='/char/fame'])>1 else {'accountId':['42'],'charId':['7']})
    if fields!=expected:errors.append(str(fields))
    if scenario=='detail-late' and expected['charId']==['7'] and not detail_release.wait(60):errors.append('stale fame capture missing')
    reply=(ROOT/'tests/fixtures/death-fame.xml').read_bytes()
    if expected['charId']==['100']:reply=reply.replace(b'id="7"',b'id="100"').replace(b'FameFixture',b'Legend1')
   else:errors.append(self.path);reply=b'<Error>Unexpected route</Error>'
   self.send_response(200);self.send_header('Content-Length',str(len(reply)));self.end_headers()
   try:self.wfile.write(reply)
   except (BrokenPipeError,ConnectionResetError):pass
  def log_message(self,*args):pass
 with ThreadingHTTPServer(('127.0.0.1',0),App) as http,tempfile.TemporaryDirectory() as temp:
  threading.Thread(target=http.serve_forever,daemon=True).start()
  directory=Path(temp);account=directory/'account.json';account.write_text(json.dumps({'username':'Fixture','password':'disposable-fixture'}))
  shots={}
  def capture(ms,name):
   path=a.output/f'{scenario}-{name}.png';shots[name]=path
   return f'{ms} capture {path}'
  lines=['1000 click 500 544']
  if scenario=='success':
   lines += [capture(1200,'pending'),capture(2200,'top'),'2300 move 300 135',capture(2400,'hover'),
     '2450 focus-lost',capture(2480,'hover-cleared'),'2500 move 799 590','2520 down 120 80',capture(2550,'tab-pressed'),'2570 focus-lost','2590 up 120 80','2600 resize 1280 720',capture(3000,'720'),'3100 resize 1600 1200',capture(3500,'2x'),
     '3600 resize 3840 2160',capture(4300,'4k'),'4400 resize 800 600',capture(4800,'restored'),
     '5000 click 300 250',capture(5500,'detail'),'5600 wheel 100',capture(5900,'detail-top'),'6000 key Return',capture(6400,'detail-back'),
     '6500 wheel -1',capture(6900,'wheel'),'7000 down 784 180','7020 move 784 480','7040 up 784 480',capture(7500,'bottom'),
     '7600 down 784 480','7620 move 784 130','7640 focus-lost','7680 up 784 130',capture(8100,'focus'),
     '8200 click 120 80',capture(8600,'empty'),'8700 click 220 80',capture(8900,'all-error'),capture(10500,'all'),
     '10600 click 40 80',capture(11000,'cached'),'11100 key Escape',capture(11600,'title'),'11700 click 500 544',capture(12400,'reopen'),'12600 key Return','13000 quit']
  elif scenario=='error':
   lines += [capture(1200,'pending'),capture(4800,'error'),'4900 click 400 382',capture(5200,'retry'),capture(8500,'error-again'),'8600 key Escape',capture(9000,'title'),'9400 quit']
  elif scenario=='late':
   # Switch away from the pending week, close and reopen. Same screen enum
   # must not permit the old week generation to populate the new screen.
   lines += ['1100 click 120 80','1150 key Escape',capture(1200,'title'),'1300 click 500 544',capture(1400,'pending'),
     capture(2400,'new-week'),'2500 click 120 80',capture(3000,'empty'),'3100 key Escape',capture(3500,'title-after'),'3700 quit']
  elif scenario=='detail-late':
   lines += [capture(1200,'pending'),capture(2200,'top'),'2300 click 300 250','2400 key Return','2500 click 300 138',
     capture(2600,'detail-pending'),capture(3400,'detail-new'),'3500 key Return',capture(3900,'back'),'4000 key Return','4300 quit']
  else:
   # Quit while the worker owns a pending request. The fixture releases after
   # a real input/capture boundary, then the Startup destructor drains safely.
   lines += [capture(1200,'pending'),'1300 quit']
  if scenario=='success':lines=[str(int(line.split(' ',1)[0])+(1500 if int(line.split(' ',1)[0])>=2300 else 0))+' '+line.split(' ',1)[1] for line in lines]
  script=directory/'input.script';script.write_text('\n'.join(lines)+'\n')
  cmd=[str(a.binary_dir.resolve().parent/'realm_client'),'--server','127.0.0.1','--app-port',str(http.server_port),
       '--account-file',str(account),'--settings-file',str(directory/'settings.json'),'--benchmark','--frames','100000',
       '--fixture-step-ms','20','--stress-delay','2','--script',str(script),'--telemetry',str(a.output/f'{scenario}.jsonl')]
  if not a.visible:cmd.append('--hidden')
  def release():
   while not stop.is_set() and not pending.exists():time.sleep(.01)
   if pending.exists():
    if scenario=='teardown':time.sleep(.25)
    held.set()
   if scenario=='detail-late':
    while not stop.is_set() and not detail_pending.exists():time.sleep(.01)
    if detail_pending.exists():detail_release.set()
  observer=threading.Thread(target=release,daemon=True);observer.start()
  run=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=180)
  stop.set();held.set();detail_release.set();observer.join(1);http.shutdown()
  (a.output/f'{scenario}.log').write_text(run.stdout+run.stderr)
  assert run.returncode==0,run.stdout+run.stderr
  assert not errors,errors
  assert '[rmlui error]' not in run.stderr,run.stderr
  assert not any(s in run.stderr for s in ('ERROR: AddressSanitizer','runtime error:','LeakSanitizer')),run.stderr
  fame=[f for path,f in posts if path=='/char/fame'];lists=[f['timespan'][0] for path,f in posts if path=='/fame/list']
  if scenario=='success':
   assert fame==[{'accountId':['42'],'charId':['7']}],posts
   assert lists==['week','month','all','all','week'],posts
   im={n:Image.open(path).convert('RGB') for n,path in shots.items()}
   assert im['720'].size==(1280,720) and im['restored'].size==(800,600)
   if not a.visible:assert im['2x'].size==(1600,1200) and im['4k'].size==(3840,2160)
   else:
    # The desktop WM clamps oversized visible windows to available displays.
    # Hidden runs independently require exact 1200p/4K drawables.
    assert im['2x'].height>=720 and im['4k'].height>=im['2x'].height and im['4k'].width>=im['2x'].width
   assert ImageChops.difference(im['top'].crop((10,110,766,500)),im['bottom'].crop((10,110,766,500))).getbbox(),'scroll moves rows'
   # Hover black overlay is visibly darker while text/equipment retain color.
   top=im['top'].crop((15,115,60,165));hover=im['hover'].crop((15,115,60,165))
   assert sum(sum(rgb) for rgb in hover.getdata())<sum(sum(rgb) for rgb in top.getdata()),'source row hover darkens background'
   cleared=im['hover-cleared'].crop((15,115,60,165))
   assert sum(sum(rgb) for rgb in cleared.getdata())>sum(sum(rgb) for rgb in hover.getdata()),'focus loss clears composed row hover'
  elif scenario=='error':assert len(lists)==6,posts
  elif scenario=='late':assert lists==['week','week','month'],posts
  elif scenario=='detail-late':assert fame==[{'accountId':['42'],'charId':['7']},{'accountId':['90'],'charId':['100']}],posts
  else:assert lists==['week'],posts
  results[scenario]={'requests':posts,'captures':len(shots),'command':cmd,'capture_dimensions':{name:Image.open(path).size for name,path in shots.items()}}
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('Legends SDL tabs/cache/scroll/hover/resize, fame identity/back, empty/error/retry, stale close/reopen and teardown pass')
