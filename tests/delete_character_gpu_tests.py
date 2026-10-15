#!/usr/bin/env python3
"""Production SDL character deletion against disposable local HTTP peers; no live backend.

Every scenario runs once with a temporary account file and a ThreadingHTTPServer on
127.0.0.1:0 standing in for the app server. The client only ever talks to that peer.
"""
import argparse
import json
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary-dir', type=Path, default=ROOT / 'build/tests')
p.add_argument('--output', type=Path, default=ROOT / 'build/tests/delete-character-gpu')
p.add_argument('--scenarios', default='')
p.add_argument('--visible', action='store_true')
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=True)

CREDENTIALS = {'username': ['Fixture'], 'password': ['disposable-fixture']}
# Character id, ObjectType: Wizard, Archer, Rogue. The list shows the account name, "Fixture".
CHARACTERS = [(5, 782), (6, 775), (7, 768)]
PLAY = (400, 544)
CANCEL, DELETE, CLOSE = (325, 336), (475, 336), (400, 314)
SIZES = [(1280, 720), (1600, 1200), (3840, 2160), (800, 600)]


def x_at(index):
    """Center of a CurrentCharacterRect delete cross in 800x600 design pixels."""
    return 336, 145 + 63 * index


def scaled(point, width=800, height=600):
    scale = height / 600
    return round((width - 800 * scale) / 2 + point[0] * scale), round(point[1] * scale)


def click(ms, point, size=(800, 600)):
    return ms, 'click %d %d' % scaled(point, *size)


def char_list(chars):
    return ('<Chars nextCharId="9" maxNumChars="4"><Account><AccountId>42</AccountId><Name>Fixture</Name></Account>' + ''.join(
        f'<Char id="{i}"><ObjectType>{t}</ObjectType><Level>20</Level><Exp>{100 - i}</Exp><CurrentFame>400</CurrentFame></Char>'
        for i, t in chars) + '</Chars>').encode()


def crop_diff(first, second, box):
    return ImageChops.difference(first.crop(box), second.crop(box)).getbbox()


def rows_diff(first, second, count=3):
    """Compare whole character rows; the gaps and rows below show the animated map and a random class icon."""
    return [i for i in range(count) if crop_diff(first, second, (10, 116 + 63 * i, 366, 175 + 63 * i))]


def dimmed(image, box):
    """The DialogsView backdrop: 0x151515 at .6 alpha over the screen beneath."""
    return image.crop(box).point(lambda v: round(v * .4 + 21 * .6))


def rows_kept_under_dialog(before, shown):
    """Rows left of the dialog are still the original rows, merely dimmed, so the character was not removed."""
    for i in (0, 1, 2):
        box = (10, 116 + 63 * i, 240, 175 + 63 * i)
        assert mean_difference(dimmed(before, box), shown.crop(box)) < 3, f'row {i} changed while the dialog showed'


def mean_difference(first, second):
    return sum(ImageStat.Stat(ImageChops.difference(first, second)).mean) / 3


def kinds(posts):
    return [path for path, _ in posts]


def deletes(posts):
    return [fields for path, fields in posts if path == '/char/delete']


def delete_fields(char_id):
    return {**CREDENTIALS, 'charId': [str(char_id)]}


def dialog_open(image, size=(800, 600)):
    """The white Cancel button interior left of its label is only present while the confirmation shows."""
    r, g, b = image.getpixel(scaled((275, 336), *size))
    return min(r, g, b) > 225


def row_region(index, size=(800, 600)):
    left, top = scaled((12, 116 + 63 * index), *size)
    scale = size[1] / 600
    return left, top, left + round(300 * scale), top + round(55 * scale)


# Scenario table. plan: replies for successive /char/delete requests ('ok', 'error:<text>', 'drop',
# 'drop-deleted', 'hold'); steps: (fixture ms, script line or ('capture', name)); hold_on: a capture
# whose appearance releases the held reply after hold_delay seconds.
def build():
    S = {}

    S['cancel'] = dict(plan=[], steps=[
        click(1000, PLAY), (1600, ('capture', 'list')), (1650, 'move 600 300'), (1700, ('capture', 'rest')),
        click(1800, x_at(1)), (2100, ('capture', 'confirm')), click(2200, CANCEL), (2300, 'move 600 300'), (2500, ('capture', 'after-cancel-button')),
        click(2600, x_at(1)), (2900, ('capture', 'confirm-again')), (3000, 'key Escape'), (3100, 'move 600 300'), (3300, ('capture', 'after-escape')),
        click(3400, x_at(0)), (3600, 'key Return'), (3800, ('capture', 'return-keeps-dialog')), (3900, 'key Escape'),
        # A press that starts on Delete but is released elsewhere never confirms.
        click(4000, x_at(2)), (4300, 'down 475 336'), (4350, 'move 100 100'), (4400, 'up 100 100'),
        (4700, ('capture', 'press-released-elsewhere')), (4800, 'key Escape'), (5000, ('capture', 'final')), (5100, 'quit')])

    S['delete'] = dict(plan=['ok'], steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)), (2100, ('capture', 'confirm')),
        click(2200, DELETE), (2250, 'move 600 300'), (3400, ('capture', 'after')), (3500, 'quit')])

    S['error'] = dict(plan=['error:Issue deleting character'] * 3, steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)),
        click(2000, DELETE), (3400, ('capture', 'error')), click(3500, CLOSE), (3900, ('capture', 'closed')), (4000, 'quit')])

    S['error-escape'] = dict(plan=['error:Account in use!'] * 6, steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)),
        click(2000, DELETE), (3400, ('capture', 'error')), (3500, 'key Escape'), (3900, ('capture', 'closed')),
        click(4000, x_at(1)), click(4300, DELETE), (4400, ('capture', 'second-error-pending')), (5600, ('capture', 'second-error')),
        (5700, 'key Return'), (6000, ('capture', 'second-closed')), (6100, 'quit')])

    S['retry'] = dict(plan=['error:Issue deleting character', 'ok'], steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)),
        click(2000, DELETE), (3400, ('capture', 'after')), (3500, 'quit')])

    S['retry-second'] = dict(plan=['error:Invalid account.', 'error:Issue deleting character', 'ok'], steps=[
        click(1000, PLAY), click(1800, x_at(0)), click(2000, DELETE), (3400, ('capture', 'after')), (3500, 'quit')])

    S['drop'] = dict(plan=['drop-deleted'], steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)),
        click(2000, DELETE), (3000, ('capture', 'error')), (3100, 'key Return'), (3700, ('capture', 'refreshed')), (3800, 'quit')])

    S['drop-kept'] = dict(plan=['drop'], steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)),
        click(2000, DELETE), (3000, ('capture', 'error')), click(3100, CLOSE), (3700, ('capture', 'refreshed')), (3800, 'quit')])

    S['garbage'] = dict(plan=['raw:<Success>junk</Success>'], steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'), click(1800, x_at(1)), click(2000, DELETE),
        (3000, ('capture', 'error')), (3100, 'key Escape'), (3700, ('capture', 'refreshed')), (3800, 'quit')])

    S['focus-loss'] = dict(plan=['ok'], steps=[
        click(1000, PLAY), (1600, ('capture', 'before')), (1650, 'move 600 300'),
        # Focus lost between press and release of the cross: no dialog.
        (1700, 'down %d %d' % x_at(1)), (1750, 'focus-lost'), (1800, 'up %d %d' % x_at(1)), (2100, ('capture', 'cross-focus-lost')),
        click(2200, x_at(1)), (2500, ('capture', 'confirm')),
        # Same on the Delete button: nothing is sent, the dialog survives the focus loss.
        (2600, 'down 475 336'), (2650, 'focus-lost'), (2700, 'up 475 336'), (3000, ('capture', 'delete-focus-lost')),
        (3100, 'key Escape'), (3400, ('capture', 'cancelled')), (3500, 'quit')])

    S['hold'] = dict(plan=['hold'], hold_on='still-deleting', hold_delay=.2, steps=[
        click(1000, PLAY), (1650, 'move 600 300'), click(1800, x_at(1)), click(2000, DELETE), (2400, ('capture', 'deleting')),
        # Nothing reaches the screen under the Deleting Character... dialog while the request is held.
        (2500, 'key Escape'), (2550, 'key Return'), click(2600, x_at(2)), click(2700, DELETE), click(2800, (760, 14)),
        click(2900, (60, 544)), (3000, 'focus-lost'), (3100, 'resize 1280 720'), (3300, 'resize 800 600'), (3600, ('capture', 'still-deleting')),
        (9000, ('capture', 'after')), (9100, 'quit')])

    S['quit-held'] = dict(plan=['hold'], hold_on='deleting', hold_delay=1.0, steps=[
        click(1000, PLAY), (1650, 'move 600 300'), click(1800, x_at(1)), click(2000, DELETE), (2400, ('capture', 'deleting')), (2500, 'quit')])

    def resize_steps():
        steps = [click(1000, PLAY), (1650, 'move 600 300'), click(1800, x_at(1)), (2100, ('capture', 'open-800x600'))]
        t = 2200
        for width, height in SIZES:
            steps += [(t, f'resize {width} {height}'), (t + 400, ('capture', f'open-{width}x{height}'))]
            t += 600
        # The dialog is back at the original size; repeat at 4K, where hit areas must follow the scale.
        steps += [(t, 'resize 3840 2160'), (t + 300, ('capture', 'delete-4k-open')), click(t + 400, DELETE, (3840, 2160)),
                  (t + 1500, ('capture', 'deleted-4k')), (t + 1600, 'resize 800 600'), (t + 1900, ('capture', 'deleted-restored')), (t + 2000, 'quit')]
        return steps
    S['resize'] = dict(plan=['ok'], steps=resize_steps())
    return S


def check(name, env):
    posts, shots = env['posts'], env['shots']
    delete_posts = deletes(posts)

    def image(key):
        return shots[key]
    if name == 'cancel':
        assert not delete_posts, posts
        assert kinds(posts) == ['/account/verify', '/char/list'], posts
        assert not dialog_open(image('list')) and dialog_open(image('confirm')) and dialog_open(image('confirm-again'))
        assert dialog_open(image('press-released-elsewhere')), 'releasing away from Delete leaves the confirmation open'
        for key in ('after-cancel-button', 'after-escape', 'final'):
            assert not dialog_open(image(key)), key
            assert not rows_diff(image('rest'), image(key)), f'{key} changed the character list'
        assert dialog_open(image('return-keeps-dialog')), 'Enter does not confirm a deletion'
    elif name == 'delete':
        assert delete_posts == [delete_fields(6)], posts
        assert kinds(posts) == ['/account/verify', '/char/list', '/char/delete', '/char/list'], posts
        assert dialog_open(image('confirm')) and not dialog_open(image('after'))
        # The Archer row is gone and the Rogue row moved up beneath the Wizard.
        assert not crop_diff(image('before'), image('after'), row_region(0))
        assert not crop_diff(image('before').crop(row_region(2)), image('after').crop(row_region(1)), (60, 0, 300, 55)), 'rogue moved up'
        assert env['chars'] == [(5, 782), (7, 768)]
    elif name in ('error', 'error-escape'):
        attempts = 3 if name == 'error' else 6  # error-escape runs two deletions of three attempts each
        assert delete_posts == [delete_fields(6)] * attempts, posts
        assert kinds(posts) == ['/account/verify', '/char/list'] + ['/char/delete'] * attempts, posts
        assert not dialog_open(image('error')), 'the confirmation was replaced by the error dialog'
        rows_kept_under_dialog(image('before'), image('error'))
        assert not rows_diff(image('before'), image('closed')), 'explicit errors keep the list unchanged'
        assert crop_diff(image('before'), image('error'), (250, 230, 550, 370)), 'error dialog is drawn'
        assert env['chars'] == CHARACTERS
    elif name == 'retry':
        assert delete_posts == [delete_fields(6)] * 2, posts
        assert kinds(posts) == ['/account/verify', '/char/list', '/char/delete', '/char/delete', '/char/list'], posts
        assert env['chars'] == [(5, 782), (7, 768)]
        assert not crop_diff(image('before').crop(row_region(2)), image('after').crop(row_region(1)), (60, 0, 300, 55))
    elif name == 'retry-second':
        assert delete_posts == [delete_fields(5)] * 3, posts
        assert kinds(posts)[-1] == '/char/list' and len(kinds(posts)) == 6, posts
        assert env['chars'] == [(6, 775), (7, 768)]
    elif name in ('drop', 'drop-kept', 'garbage'):
        assert len(delete_posts) == 1 and delete_posts[0] == delete_fields(6), posts
        assert kinds(posts) == ['/account/verify', '/char/list', '/char/delete', '/char/list'], f'ambiguous replies are not retried, the list is refreshed once closed: {posts}'
        # While the error shows, the row is still listed; after closing, the refreshed server list decides.
        if name == 'drop':
            assert env['chars'] == [(5, 782), (7, 768)]
        else:
            assert env['chars'] == CHARACTERS
            assert not rows_diff(image('before'), image('refreshed'))
        assert crop_diff(image('before'), image('error'), (250, 250, 550, 350)), 'the error dialog is drawn'
        rows_kept_under_dialog(image('before'), image('error'))
    elif name == 'focus-loss':
        assert not delete_posts and kinds(posts) == ['/account/verify', '/char/list'], posts
        assert not dialog_open(image('cross-focus-lost')) and dialog_open(image('confirm')) and dialog_open(image('delete-focus-lost'))
        assert not dialog_open(image('cancelled'))
        assert not rows_diff(image('before'), image('cancelled'))
    elif name == 'hold':
        assert delete_posts == [delete_fields(6)] and kinds(posts)[-1] == '/char/list' and len(posts) == 4, posts
        assert not crop_diff(image('deleting'), image('still-deleting'), (252, 288, 548, 312)), 'the Deleting dialog ignores input and resize'
        assert env['chars'] == [(5, 782), (7, 768)]
    elif name == 'quit-held':
        assert delete_posts == [delete_fields(6)], posts
    elif name == 'resize':
        assert delete_posts == [delete_fields(6)] and len(posts) == 4, posts
        base = image('open-800x600').crop((260, 270, 540, 350))
        for width, height in SIZES:
            shot = image(f'open-{width}x{height}')
            assert shot.size == (width, height), (shot.size, width, height)
            assert dialog_open(shot, (width, height)), f'dialog survives resize to {width}x{height}'
            scale = height / 600
            left, top = scaled((260, 270), width, height)
            crop = shot.crop((left, top, left + round(280 * scale), top + round(80 * scale))).resize((280, 80), Image.LANCZOS)
            assert mean_difference(base, crop) < 14, f'dialog layout at {width}x{height} matches the 800x600 layout'
        assert dialog_open(image('delete-4k-open'), (3840, 2160)) and not dialog_open(image('deleted-4k'), (3840, 2160))
        assert not dialog_open(image('deleted-restored'))
        assert env['chars'] == [(5, 782), (7, 768)]
    else:
        raise AssertionError(name)


results = {}
selected = [s for s in a.scenarios.split(',') if s]
for name, scenario in build().items():
    if selected and name not in selected:
        continue
    posts, errors = [], []
    chars = list(CHARACTERS)
    plan = list(scenario['plan'])
    release, stop = threading.Event(), threading.Event()
    hold_capture = a.output / f'{name}-{scenario["hold_on"]}.png' if scenario.get('hold_on') else None
    if hold_capture:
        hold_capture.unlink(missing_ok=True)

    class App(BaseHTTPRequestHandler):
        def reply(self, body):
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_POST(self):
            fields = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode(), keep_blank_values=True)
            posts.append((self.path, fields))
            if self.path == '/account/verify':
                self.reply(b'<Success/>')
            elif self.path == '/char/list':
                self.reply(char_list(chars))
            elif self.path == '/char/delete':
                if not plan:
                    errors.append('unexpected extra /char/delete')
                    self.reply(b'<Error>Issue deleting character</Error>')
                    return
                step = plan.pop(0)
                if step == 'hold' and not release.wait(60):
                    errors.append('held reply was never released')
                if step in ('ok', 'hold', 'drop-deleted'):
                    chars[:] = [c for c in chars if str(c[0]) != fields.get('charId', [''])[0]]
                if step in ('ok', 'hold'):
                    self.reply(b'<Success/>')
                elif step.startswith('error:'):
                    self.reply(f'<Error>{step[6:]}</Error>'.encode())
                elif step.startswith('raw:'):
                    self.reply(step[4:].encode())
                # drop / drop-deleted: close the connection without a response.
            else:
                errors.append(self.path)
                self.reply(b'<Error>Unexpected route</Error>')

        def log_message(self, *args):
            pass

    with ThreadingHTTPServer(('127.0.0.1', 0), App) as http, tempfile.TemporaryDirectory() as temp:
        threading.Thread(target=http.serve_forever, daemon=True).start()
        directory = Path(temp)
        account = directory / 'account.json'
        account.write_text(json.dumps({'username': 'Fixture', 'password': 'disposable-fixture'}))
        shots = {}
        lines = []
        for ms, step in scenario['steps']:
            if isinstance(step, tuple):
                path = a.output / f'{name}-{step[1]}.png'
                path.unlink(missing_ok=True)
                shots[step[1]] = path
                lines.append(f'{ms} capture {path}')
            else:
                lines.append(f'{ms} {step}')
        script = directory / 'input.script'
        script.write_text('\n'.join(lines) + '\n')
        command = [str(a.binary_dir.resolve().parent / 'realm_client'), '--server', '127.0.0.1',
                   '--app-port', str(http.server_port), '--account-file', str(account), '--settings-file', str(directory / 'settings.json'),
                   '--benchmark', '--frames', '100000', '--fixture-step-ms', '20', '--stress-delay', '2',
                   '--script', str(script), '--telemetry', str(a.output / f'{name}.jsonl')]
        if not a.visible:
            command.append('--hidden')

        def observer():
            while not stop.is_set() and not (hold_capture and hold_capture.exists()):
                time.sleep(.01)
            if hold_capture and hold_capture.exists():
                time.sleep(scenario.get('hold_delay', 0))
                release.set()
        if hold_capture:
            threading.Thread(target=observer, daemon=True).start()
        else:
            release.set()
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=240)
        stop.set()
        release.set()
        http.shutdown()
        (a.output / f'{name}.log').write_text(run.stdout + run.stderr)
        assert run.returncode == 0, run.stdout + run.stderr
        assert not errors, (name, errors)
        assert '[rmlui error]' not in run.stderr, run.stderr
        assert not any(s in run.stderr for s in ('ERROR: AddressSanitizer', 'runtime error:', 'LeakSanitizer')), run.stderr
        env = dict(posts=posts, chars=chars, shots={k: Image.open(v).convert('RGB') for k, v in shots.items() if v.exists()})
        missing = [k for k, v in shots.items() if not v.exists()]
        assert not missing, (name, 'missing captures', missing)
        try:
            check(name, env)
        except AssertionError as error:
            raise AssertionError(f'{name}: {error!r}') from error
        results[name] = {'requests': [(path, fields) for path, fields in posts], 'captures': {k: Image.open(v).size for k, v in shots.items()},
                        'command': command}
(a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print('Character deletion: cancel (button/Escape/focus loss), exact charId, retries, retained rows on errors, ambiguous-reply refresh, held-request modal, quit while held and resize at 800x600/720p/1200p/4K pass')
