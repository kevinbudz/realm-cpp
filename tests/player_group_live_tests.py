#!/usr/bin/env python3
"""Native group selection/Session teleport against disposable real server worlds."""
from pathlib import Path
import argparse,subprocess,tempfile,time,sqlite3,xml.etree.ElementTree as ET
from live_server_tests import free_port,request,Proxy
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/realm_teleport_session_inputs');a=p.parse_args()
app,game=free_port(),free_port();server_root=ROOT.parent/'realm-server';outputs=[]
with tempfile.TemporaryDirectory(prefix='realm-group-live-') as directory:
 work=Path(directory);settings=ET.parse(server_root/'Settings.xml');settings.getroot().set('res',str(server_root/'Resources'));settings.getroot().set('db',str(work/'Database'));settings.find('Ports').text=f'{app}:{game}';settings.write(work/'Settings.xml')
 with (work/'server.log').open('w+') as log:
  server=subprocess.Popen(['dotnet',str(a.server_dll.resolve())],cwd=work,stdout=log,stderr=subprocess.STDOUT);peers=[]
  try:
   deadline=time.monotonic()+45
   while True:
    if server.poll() is not None:log.seek(0);raise RuntimeError(log.read())
    try:
     if request(app,'/char/list',{}).tag=='Chars':break
    except (OSError,TimeoutError):pass
    assert time.monotonic()<deadline;time.sleep(.1)
   password='group-private-fixture';names=['GroupActor','Newer','Older']
   for name,source in zip(names,['127.0.0.1','127.0.0.2','127.0.0.3']):
    assert request(app,'/account/register',dict(newUsername=name,newPassword=password),source).tag=='Success'
    r=subprocess.run([str(ROOT/'build/realm_session'),'--server','127.0.0.1','--port',str(game),'--username',name,'--password-stdin','--seconds','1','--create','0x030e'],input=password+'\n',capture_output=True,text=True,timeout=15)
    assert r.returncode==0,r.stdout+r.stderr
   db=sqlite3.connect(work/'Database/realm.db');actor_id=int(db.execute('SELECT value FROM kv WHERE path=?',('login.username.GroupActor',)).fetchone()[0]);key='account.'+str(actor_id);account=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(key,)).fetchone()[0])
   for tag,value in [('GuildName','Group Guild'),('GuildRank','40')]:
    node=account.find(tag)
    if node is None:node=ET.SubElement(account,tag)
    node.text=value
   db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(account,encoding='unicode'),key));db.commit();db.close()
   def friend(name):
    proc=subprocess.Popen([str(ROOT/'build/realm_session'),'--server','127.0.0.1','--port',str(game),'--username',name,'--password-stdin','--seconds','22','--character','0'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    proc.stdin.write(password+'\n');proc.stdin.close();proc.stdin=None;peers.append(proc)
   friend('Older');time.sleep(3.7);friend('Newer');time.sleep(.3)
   proxy=Proxy(game)
   r=subprocess.run([str(a.client.resolve()),'live',str(proxy.port),'GroupActor','Newer','Older'],input=password+'\n',text=True,capture_output=True,timeout=20)
   counts=proxy.finish();assert r.returncode==0,r.stdout+r.stderr;assert counts[22]==2 and counts[48]==2,counts
   outputs.append(r.stdout.strip()+f' exact TELEPORT=2 / GOTOACK=2; cooldown retry sent no packet');print(outputs[-1])
   # Real GuildHall MAPINFO disables teleport; even direct Session calls send none.
   for peer in peers:peer.terminate();peer.communicate(timeout=5)
   peers=[];proxy=Proxy(game)
   r=subprocess.run([str(a.client.resolve()),'hall',str(proxy.port),'GroupActor'],input=password+'\n',text=True,capture_output=True,timeout=12)
   counts=proxy.finish();assert r.returncode==0,r.stdout+r.stderr;assert counts[22]==0,counts
   outputs.append(r.stdout.strip()+' real GuildHall MAPINFO / TELEPORT=0');print(outputs[-1])
  finally:
   for peer in peers:
    if peer.poll() is None:peer.terminate()
    peer.communicate(timeout=5)
   server.terminate()
   try:server.wait(5)
   except subprocess.TimeoutExpired:server.kill();server.wait()
out=ROOT/'build/player-group-reference';out.mkdir(parents=True,exist_ok=True);(out/('live-sanitize.log' if 'sanitize' in str(a.client) else 'live.log')).write_text('\n'.join(outputs)+'\n')
print('Isolated real-server player-group teleport and map restriction passed; user account/settings preserved')
