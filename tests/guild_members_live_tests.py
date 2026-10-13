#!/usr/bin/env python3
"""Native guild list/controller/Session TCP actions against isolated real server."""
from pathlib import Path
import argparse,sqlite3,subprocess,tempfile,time,xml.etree.ElementTree as ET
from live_server_tests import free_port,request,Proxy
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/realm_guild_members_tests');args=p.parse_args()
outputs=[]
for rank,online in [(40,False),(40,True),(10,False)]:
 app,game=free_port(),free_port();server_root=ROOT.parent/'realm-server'
 with tempfile.TemporaryDirectory(prefix='realm-members-live-') as directory:
  work=Path(directory);settings=ET.parse(server_root/'Settings.xml');settings.getroot().set('res',str(server_root/'Resources'));settings.getroot().set('db',str(work/'Database'));settings.find('Ports').text=f'{app}:{game}';settings.write(work/'Settings.xml')
  with (work/'server.log').open('w+') as log:
   server=subprocess.Popen(['dotnet',str(args.server_dll.resolve())],cwd=work,stdout=log,stderr=subprocess.STDOUT)
   friend=None
   try:
    deadline=time.monotonic()+45
    while True:
     if server.poll() is not None:log.seek(0);raise RuntimeError(log.read())
     try:
      if request(app,'/char/list',{}).tag=='Chars':break
     except (OSError,TimeoutError):pass
     assert time.monotonic()<deadline,'Server startup timeout';time.sleep(.1)
    username,target,password='GuildActor','GuildTarget','members-private-fixture'
    for name,source in [(username,'127.0.0.1'),(target,'127.0.0.2')]:
     assert request(app,'/account/register',dict(newUsername=name,newPassword=password),source).tag=='Success'
     result=subprocess.run([str(ROOT/'build/realm_session'),'--server','127.0.0.1','--port',str(game),'--username',name,'--password-stdin','--seconds','1','--create','0x030e'],input=password+'\n',text=True,capture_output=True,timeout=15)
     assert result.returncode==0,result.stdout+result.stderr
    db=sqlite3.connect(work/'Database/realm.db');ids=[]
    for name,r,fame in [(username,rank,700),(target,0,125)]:
     id=int(db.execute('SELECT value FROM kv WHERE path=?',('login.username.'+name,)).fetchone()[0]);ids.append(id)
     key='account.'+str(id);account=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',(key,)).fetchone()[0])
     for tag,value in [('GuildName','Native Guild'),('GuildRank',str(r))]:
      node=account.find(tag)
      if node is None:node=ET.SubElement(account,tag)
      node.text=value
     account.find('Stats/Fame').text=str(fame)
     db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(account,encoding='unicode'),key))
    for key,value in [('guild.NATIVE GUILD',str(ids[0])),('guild.NATIVE GUILD.members',','.join(map(str,ids))),('guild.NATIVE GUILD.fame','321'),('guild.NATIVE GUILD.level','2')]:db.execute('INSERT OR REPLACE INTO kv(path,value) VALUES(?,?)',(key,value))
    db.commit()
    # Account caches from creation were released on disconnect. Real list reads
    # authoritative online accounts and otherwise loads the seeded DB records.
    if online:
     friend=subprocess.Popen([str(ROOT/'build/realm_session'),'--server','127.0.0.1','--port',str(game),'--username',target,'--password-stdin','--seconds','6','--character','0'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
     friend.stdin.write(password+'\n');friend.stdin.close();friend.stdin=None;time.sleep(.5)
    proxy=Proxy(game)
    result=subprocess.run([str(args.client.resolve()),'live',str(app),str(proxy.port),username,target,str(rank)],input=password+'\n',text=True,capture_output=True,timeout=22)
    counts=proxy.finish();assert result.returncode==0,result.stdout+result.stderr
    assert counts[45]==(1 if rank==10 else 2) and counts[38]==1,counts
    assert counts[5]==0,'Actions never send player chat instead of dedicated packet'
    rows=request(app,'/guild/listMembers',dict(username=username,password=password))
    assert [m.findtext('Name') for m in rows.findall('Member')]==([username,target] if rank==10 else [username]),ET.tostring(rows)
    stored=ET.fromstring(db.execute('SELECT value FROM kv WHERE path=?',('account.'+str(ids[1]),)).fetchone()[0])
    assert stored.findtext('GuildRank')=='0'
    assert (stored.findtext('GuildName') or '')==('Native Guild' if rank==10 else '')
    if friend:
     stdout,stderr=friend.communicate(timeout=10);assert friend.returncode==0,stdout+stderr
     assert 'has been promoted to Member.' in stdout and 'has been demoted to Initiate.' in stdout and 'You have been kicked from the guild.' in stdout,stdout
    message=result.stdout.strip()+f' (target {"online" if online else "offline"}; exact dedicated packets and persisted DB checked)';outputs.append(message);print(message)
    db.close()
   finally:
    if friend and friend.poll() is None:friend.terminate();friend.wait(5)
    server.terminate()
    try:server.wait(5)
    except subprocess.TimeoutExpired:server.kill();server.wait()
out=ROOT/'build/guild-members-reference';out.mkdir(parents=True,exist_ok=True);(out/('live-sanitize.log' if 'sanitize' in str(args.client) else 'live.log')).write_text('\n'.join(outputs)+'\n')
print('Isolated real guild member actions passed; saved user accounts/settings unchanged')
