#!/usr/bin/env python3
"""Real server Drunk condition -> native blur, solely a disposable SQLite account."""
from pathlib import Path
import argparse,subprocess,tempfile,time,sqlite3,xml.etree.ElementTree as ET
from live_server_tests import free_port,request,Proxy
ROOT=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/tests/realm_postfx_session_inputs');a=p.parse_args()
server_root=ROOT.parent/'realm-server';out=ROOT/'build/tests/postfx-reference';out.mkdir(parents=True,exist_ok=True);app,game=free_port(),free_port()
items={o.get('id'):int(o.get('type'),0) for o in ET.parse(server_root/'Resources/GameData/EmbeddedData_EquipCXML.xml').getroot()}
classids={o.get('id'):int(o.get('type'),0) for o in ET.parse(server_root/'Resources/GameData/EmbeddedData_PlayersCXML.xml').getroot()}
with tempfile.TemporaryDirectory(prefix='realm-postfx-live-') as directory:
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
   name='FxDrunk';password='postfx-private-fixture';assert request(app,'/account/register',dict(newUsername=name,newPassword=password),'127.0.0.1').tag=='Success'
   r=subprocess.run([str(a.client.resolve().parent/'realm_session'),'--server','localhost','--port',str(game),'--username',name,'--password-stdin','--seconds','1','--create',str(classids['Wizard'])],input=password+'\n',text=True,capture_output=True,timeout=15);assert r.returncode==0,r.stdout+r.stderr
   deadline=time.monotonic()+5
   while request(app,'/char/list',dict(username=name,password=password)).tag!='Chars':
    assert time.monotonic()<deadline;time.sleep(.1)
   time.sleep(.5);db=sqlite3.connect(work/'Database/realm.db');id=int(db.execute('SELECT value FROM kv WHERE path=?',('login.username.'+name,)).fetchone()[0]);key='char.'+str(id)+'.0';char=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(key,)).fetchone()[0]);slots=list(map(int,char.findtext('Inventory').split(',')));slots[4]=items['Pirate Rum'];char.find('Inventory').text=','.join(map(str,slots));db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(char,encoding='unicode'),key));db.commit();db.close()
   seeded=request(app,'/char/list',dict(username=name,password=password));assert int(seeded.find('Char').findtext('Equipment').split(',')[4])==items['Pirate Rum'],'Seed overwritten before native LOAD'
   proxy=Proxy(game);r=subprocess.run([str(a.client.resolve()),str(proxy.port),name],input=password+'\n',text=True,capture_output=True,timeout=25);counts=proxy.finish();assert r.returncode==0,r.stdout+r.stderr+str(counts);assert counts[13]==1,counts
   variant='sanitize' if 'sanitize' in str(a.client) else 'release';(out/('live-'+variant+'.log')).write_text(r.stdout+'USEITEM=1; saved account untouched\n');print(r.stdout.strip())
  except BaseException:
   log.flush();log.seek(0);(out/'live-failure-server.log').write_text(log.read().replace('postfx-private-fixture','[redacted]'));raise
  finally:
   server.terminate()
   try:server.wait(5)
   except subprocess.TimeoutExpired:server.kill();server.wait()
print('Isolated real-server Drunk blur passed')
