#!/usr/bin/env python3
"""Fresh isolated real-server native movement/party-arrow/menu/teleport checks."""
from pathlib import Path
import argparse,subprocess,tempfile,time,xml.etree.ElementTree as ET
from live_server_tests import free_port,request,Proxy
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/realm_party_arrow_session_inputs');a=p.parse_args()
app,game=free_port(),free_port();server_root=ROOT.parent/'realm-server'
with tempfile.TemporaryDirectory(prefix='realm-arrows-live-') as directory:
 work=Path(directory);settings=ET.parse(server_root/'Settings.xml');settings.getroot().set('res',str(server_root/'Resources'));settings.getroot().set('db',str(work/'Database'));settings.find('Ports').text=f'{app}:{game}';settings.write(work/'Settings.xml')
 with (work/'server.log').open('w+') as log:
  server=subprocess.Popen(['dotnet',str(a.server_dll.resolve())],cwd=work,stdout=log,stderr=subprocess.STDOUT)
  try:
   deadline=time.monotonic()+45
   while True:
    if server.poll() is not None:log.seek(0);raise RuntimeError(log.read())
    try:
     if request(app,'/char/list',{}).tag=='Chars':break
    except (OSError,TimeoutError):pass
    assert time.monotonic()<deadline;time.sleep(.1)
   password='arrows-private-fixture'
   for name,source in [('ArrowActor','127.0.0.1'),('ArrowPeer','127.0.0.2')]:
    assert request(app,'/account/register',dict(newUsername=name,newPassword=password),source).tag=='Success'
    r=subprocess.run([str(ROOT/'build/realm_session'),'--server','127.0.0.1','--port',str(game),'--username',name,'--password-stdin','--seconds','1','--create','0x030e'],input=password+'\n',capture_output=True,text=True,timeout=15);assert r.returncode==0,r.stdout+r.stderr
   actor,peer=Proxy(game),Proxy(game)
   r=subprocess.run([str(a.client.resolve()),str(actor.port),str(peer.port)],input=password+'\n',text=True,capture_output=True,timeout=40)
   ac,pc=actor.finish(),peer.finish();assert r.returncode==0,r.stdout+r.stderr
   assert ac[22]==2 and ac[48]>=2 and ac[3]==0 and ac[13]==0,ac
   assert pc[4]>10 and pc[22]==0 and pc[3]==0 and pc[13]==0,pc
   output=r.stdout.strip()+f'; wire TELEPORT=2, GOTOACK={ac[48]} (includes other entities), peer MOVE={pc[4]}, no shot/ability leakage\n';print(output)
   out=ROOT/'build/party-arrow-reference';out.mkdir(exist_ok=True,parents=True);(out/('live-sanitize.log' if 'sanitize' in str(a.client) else 'live.log')).write_text(output)
  finally:
   server.terminate()
   try:server.wait(5)
   except subprocess.TimeoutExpired:server.kill();server.wait()
print('Isolated real-server party arrows passed; disposable accounts/database preserve user data')
