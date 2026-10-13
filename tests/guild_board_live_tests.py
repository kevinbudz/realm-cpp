#!/usr/bin/env python3
"""Native board HTTP against the unchanged server and a disposable SQLite DB."""
from pathlib import Path
import argparse
import sqlite3
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from live_server_tests import free_port, request
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('server_dll',type=Path);p.add_argument('--client',type=Path,default=ROOT/'build/realm_guild_board_tests');args=p.parse_args()
app,game=free_port(),free_port();server_root=ROOT.parent/'realm-server'
with tempfile.TemporaryDirectory(prefix='realm-board-live-') as directory:
    work=Path(directory);settings=ET.parse(server_root/'Settings.xml');settings.getroot().set('res',str(server_root/'Resources'));settings.getroot().set('db',str(work/'Database'));settings.find('Ports').text=f'{app}:{game}';settings.write(work/'Settings.xml')
    with (work/'server.log').open('w+') as log:
        server=subprocess.Popen(['dotnet',str(args.server_dll.resolve())],cwd=work,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+45
            while True:
                if server.poll() is not None: log.seek(0);raise RuntimeError(log.read())
                try:
                    if request(app,'/char/list',{}).tag=='Chars': break
                except (OSError,TimeoutError): pass
                assert time.monotonic()<deadline,'Server startup timeout'
                time.sleep(.1)
            username,password='BoardFixture','board-private-fixture'
            assert request(app,'/account/register',{'newUsername':username,'newPassword':password}).tag=='Success'
            db=sqlite3.connect(work/'Database/realm.db')
            key,xml=db.execute("SELECT path,value FROM kv WHERE path LIKE 'account.%'").fetchone()
            account=ET.fromstring(xml)
            def seed(rank,guild=True):
                for name,value in [('GuildName','Board Guild' if guild else ''),('GuildRank',str(rank))]:
                    node=account.find(name)
                    if node is None: node=ET.SubElement(account,name)
                    node.text=value
                db.execute('UPDATE kv SET value=? WHERE path=?',(ET.tostring(account,encoding='unicode'),key))
                db.execute('INSERT OR REPLACE INTO kv(path,value) VALUES(?,?)',('guild.BOARD GUILD.board','Initial board'));db.commit()
            outputs=[]
            def run(mode,rank,pw=password):
                result=subprocess.run([str(args.client.resolve()),mode,str(app),username,str(rank)],input=pw+'\n',text=True,capture_output=True,timeout=15)
                assert result.returncode==0,result.stdout+result.stderr
                assert password not in result.stdout+result.stderr
                outputs.append(result.stdout.strip());print(result.stdout.strip())
            seed(20)
            run('roundtrip',20)
            stored=db.execute('SELECT value FROM kv WHERE path=?',('guild.BOARD GUILD.board',)).fetchone()[0]
            assert 'Literal + %20 & <tag> Unicode: café' in stored,stored
            seed(10);run('roundtrip',10);run('denied',20)
            assert db.execute('SELECT value FROM kv WHERE path=?',('guild.BOARD GUILD.board',)).fetchone()[0]=='Initial board'
            seed(0,False);run('error',20)
            seed(20);run('error',20,'wrong-fixture-password')
            (ROOT/'build/guild-board-reference/live.log').write_text('\n'.join(outputs)+'\nIsolated real server exact UTF8/raw board persistence, officer save, member read-only, server permission rejection, no-guild and invalid account checks passed.\n')
            db.close()
        finally:
            server.terminate()
            try: server.wait(5)
            except subprocess.TimeoutExpired: server.kill();server.wait()
print('Real server guild board HTTP checks passed; user settings and account were untouched')
