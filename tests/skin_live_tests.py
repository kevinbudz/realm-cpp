#!/usr/bin/env python3
"""Disposable real server/account/database verification for native reskin flows."""
from pathlib import Path
import argparse,subprocess,tempfile,time,sqlite3,xml.etree.ElementTree as ET
from live_server_tests import free_port,request,Proxy
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/tests/realm_skin_session_inputs');a=p.parse_args()
app,game=free_port(),free_port();server_root=ROOT.parent/'realm-server';out=ROOT/'build/tests/skin-reference';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='realm-skin-live-') as directory:
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
   password='skin-private-fixture'
   for name,source in [('SkinActor','127.0.0.1'),('SkinPeer','127.0.0.2'),('Fixture','127.0.0.3')]:assert request(app,'/account/register',dict(newUsername=name,newPassword='private-fixture' if name=='Fixture' else password),source).tag=='Success'
   db=sqlite3.connect(work/'Database/realm.db');id=int(db.execute('SELECT value FROM kv WHERE path=?',('login.username.SkinActor',)).fetchone()[0]);key='account.'+str(id);account=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(key,)).fetchone()[0]);node=account.find('OwnedSkins');node.text='836,835';account.find('Stats/Credits').text='1000';db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(account,encoding='unicode'),key));fixture_id=int(db.execute('SELECT value FROM kv WHERE path=?',('login.username.Fixture',)).fetchone()[0]);fixture_key='account.'+str(fixture_id);fixture=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(fixture_key,)).fetchone()[0]);fixture.find('OwnedSkins').text='835';fixture.find('Stats/Credits').text='1000';db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(fixture,encoding='unicode'),fixture_key));db.commit();db.close()
   http_result=subprocess.run([str(a.client.resolve().parent/'realm_skin_http_inputs'),'success',str(app),'127.0.0.1'],text=True,capture_output=True,timeout=15)
   assert http_result.returncode==0,http_result.stdout+http_result.stderr
   purchased=request(app,'/char/list',dict(username='Fixture',password='private-fixture'));assert 836 in list(map(int,purchased.findtext('OwnedSkins').split(','))) and purchased.findtext('Account/Stats/Credits')=='0',ET.tostring(purchased)
   proxy,peer=Proxy(game),Proxy(game)
   r=subprocess.run([str(a.client.resolve()),str(app),str(proxy.port),str(peer.port)],input=password+'\n',text=True,capture_output=True,timeout=25)
   counts=proxy.finish();peer_counts=peer.finish();assert r.returncode==0,r.stdout+r.stderr;assert counts[47]==6 and counts[2]==1 and peer_counts[2]==1,counts
   result=http_result.stdout.strip()+' real purchase persisted owned skin / credits=0\n'+r.stdout.strip()+f' exact RESKIN=6 / actor CREATE=1 / peer CREATE=1\n';(out/('live-sanitize.log' if 'sanitize' in str(a.client) else 'live.log')).write_text(result);print(result)
   time.sleep(.3);data=request(app,'/char/list',dict(username='SkinActor',password=password));assert data.findtext('Char/Texture')=='0',ET.tostring(data)
  finally:
   server.terminate()
   try:server.wait(5)
   except subprocess.TimeoutExpired:server.kill();server.wait()
print('Isolated real server skin checks passed; user account/settings/database preserved')
