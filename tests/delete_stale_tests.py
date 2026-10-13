#!/usr/bin/env python3
"""A late /char/delete reply must not touch a different screen, session or account.

The C++ driver changes the Startup session while the mock holds the reply, then asks the mock to release it.
Only disposable accounts and a 127.0.0.1 ThreadingHTTPServer are used.
"""
import json
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs

binary = Path(sys.argv[1]).resolve()
modes = sys.argv[2].split(',') if len(sys.argv) > 2 else ['same', 'left_game', 'renamed', 'entered_game', 'quit']


def chars(account, ids):
    return (f'<Chars nextCharId="20" maxNumChars="4"><Account><AccountId>{account}</AccountId><Name>{"Fixture" if account == 42 else "Other"}</Name></Account>' + ''.join(
        f'<Char id="{i}"><ObjectType>782</ObjectType><Level>20</Level><Exp>1</Exp><CurrentFame>1</CurrentFame></Char>' for i in ids) + '</Chars>').encode()


for mode in modes:
    posts = []
    released = threading.Event()
    state = {'Fixture': [5, 6, 7], 'Other': [6, 8, 9]}

    class App(BaseHTTPRequestHandler):
        def do_POST(self):
            fields = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(), keep_blank_values=True)
            posts.append((self.path, fields))
            user = fields.get('username', ['Fixture'])[0]
            if self.path == '/char/delete':
                assert released.wait(30), 'held delete never released'
                state[user] = [c for c in state[user] if str(c) != fields['charId'][0]]
                body = b'<Success/>'
            elif self.path == '/__release':
                released.set()
                body = b'<Success/>'
            elif self.path == '/char/list':
                body = chars(42 if user == 'Fixture' else 43, state[user])
            elif self.path == '/account/verify':
                body = b'<Success/>'
            else:
                body = b'<Error>unexpected</Error>'
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *args):
            pass

    with ThreadingHTTPServer(('127.0.0.1', 0), App) as http, tempfile.TemporaryDirectory() as temp:
        threading.Thread(target=http.serve_forever, daemon=True).start()
        account = Path(temp) / 'account.json'
        account.write_text(json.dumps({'username': 'Fixture', 'password': 'disposable-fixture'}))
        run = subprocess.run([str(binary), mode, str(http.server_port), str(account)], capture_output=True, text=True, timeout=120)
        http.shutdown()
        assert run.returncode == 0, (mode, run.stdout, run.stderr)
        assert not any(s in run.stderr for s in ('ERROR: AddressSanitizer', 'runtime error:', 'LeakSanitizer')), run.stderr
        paths = [path for path, _ in posts if path != '/__release']
        deletes = [f for path, f in posts if path == '/char/delete']
        assert deletes == [{'username': ['Fixture'], 'password': ['disposable-fixture'], 'charId': ['6']}], (mode, posts)
        lists = [f for path, f in posts if path == '/char/list']
        if mode == 'same':
            assert paths == ['/account/verify', '/char/list', '/char/delete', '/char/list'], (mode, paths)
        elif mode == 'left_game':
            assert paths == ['/account/verify', '/char/list', '/char/delete', '/char/list'], (mode, paths)
        elif mode == 'renamed':
            assert paths == ['/account/verify', '/char/list', '/char/delete', '/char/list'] and lists[-1]['username'] == ['Other'], (mode, posts)
        elif mode == 'entered_game':
            # No refresh while playing; one after returning to the menus.
            assert paths == ['/account/verify', '/char/list', '/char/delete', '/char/list'], (mode, paths)
        else:
            assert paths == ['/account/verify', '/char/list', '/char/delete'], (mode, paths)
        print(run.stdout.strip())
print('Stale character deletion replies cannot alter another screen, session or account; the destructor drains the worker')
