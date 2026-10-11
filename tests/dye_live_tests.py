#!/usr/bin/env python3
"""Real USEITEM/stat29/30 persistence and observed appearance on isolated server."""
from pathlib import Path
import argparse,json,subprocess,tempfile,time,sqlite3,xml.etree.ElementTree as ET
from live_server_tests import free_port,request,Proxy
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/realm_dye_session_inputs');a=p.parse_args()
app,game=free_port(),free_port();server_root=ROOT.parent/'realm-server';out=ROOT/'build/dye-reference';out.mkdir(parents=True,exist_ok=True)
textiles=ET.parse(server_root/'Resources/GameData/EmbeddedData_TextilesCXML.xml').getroot();items=[];tex1=0x1f0f8ff;tex2=0
for kind in [4,5,9,10]:
 channel='Tex1' if kind in [4,9] else 'Tex2';obj=next(o for o in textiles if o.find(channel) is not None and int(o.findtext(channel),0)>>24==kind)
 value=int(obj.findtext(channel),0)
 if channel=='Tex1':tex1=value
 else:tex2=value
 items.append(dict(type=int(obj.get('type'),0),tex1=tex1,tex2=tex2))
(out/'live-cases.json').write_text(json.dumps(items))
with tempfile.TemporaryDirectory(prefix='realm-dye-live-') as directory:
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
   password='dye-private-fixture'
   for name,source in [('DyeActor','127.0.0.1'),('DyePeer','127.0.0.2')]:
    assert request(app,'/account/register',dict(newUsername=name,newPassword=password),source).tag=='Success'
    r=subprocess.run([str(a.client.resolve().parent/'realm_session'),'--server','127.0.0.1','--port',str(game),'--username',name,'--password-stdin','--seconds','1','--create','0x030e'],input=password+'\n',text=True,capture_output=True,timeout=15);assert r.returncode==0,r.stdout+r.stderr
   time.sleep(.3);db=sqlite3.connect(work/'Database/realm.db');id=int(db.execute('SELECT value FROM kv WHERE path=?',('login.username.DyeActor',)).fetchone()[0]);key='account.'+str(id);account=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(key,)).fetchone()[0]);account.find('OwnedSkins').text='836';db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(account,encoding='unicode'),key))
   key='char.'+str(id)+'.0';char=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(key,)).fetchone()[0]);slots=list(map(int,char.findtext('Inventory').split(',')));slots[4:8]=[c['type'] for c in items];char.find('Inventory').text=','.join(map(str,slots));char.find('Tex1').text=str(0x1f0f8ff);char.find('Tex2').text='0';char.find('Size').text='137';db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(char,encoding='unicode'),key));db.commit();db.close()
   proxy,peer=Proxy(game),Proxy(game)
   variant='sanitize' if 'sanitize' in str(a.client) else 'release';r=subprocess.run([str(a.client.resolve()),str(proxy.port),str(peer.port),str(ROOT/'build/fonts-client'),str(out/'live-cases.json'),str(out/('live-'+variant))],input=password+'\n',text=True,capture_output=True,timeout=30)
   counts=proxy.finish();peer_counts=peer.finish();assert r.returncode==0,r.stdout+r.stderr;assert counts[13]==4 and counts[47]==1,counts
   time.sleep(.4);data=request(app,'/char/list',dict(username='DyeActor',password=password));char=data.find('Char');assert int(char.findtext('Tex1'))==tex1 and int(char.findtext('Tex2'))==tex2,ET.tostring(char);assert char.findtext('Texture')=='836';slots=list(map(int,char.findtext('Equipment').split(',')));assert slots[4:8]==[-1]*4,slots
   result=r.stdout.strip()+f'\nExactly USEITEM=4 RESKIN=1; SQLite/HTTP dyes and consumed inventory persisted\n';(out/('live-'+variant+'.log')).write_text(result);print(result)
  finally:
   server.terminate()
   try:server.wait(5)
   except subprocess.TimeoutExpired:server.kill();server.wait()
print('Isolated real server dyes passed; saved user account preserved')
