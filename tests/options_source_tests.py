#!/usr/bin/env python3
"""The native option table against Flash's own Options.as, Parameters.as and KeyCodes.as (task 12-options).

Parses every add*Options function (labels, tooltips, choice lists, order, spacers, disabled rows) and every
Parameters.setDefaults entry, and compares them with `realm_options_inputs --table`. Differences that are
deliberate are listed in EXPECTED and nowhere else.
"""
import json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLASH = ROOT.parent / 'realm-client/src/com/company'
options_as = (FLASH / 'assembleegameclient/ui/options/Options.as').read_text()
parameters_as = (FLASH / 'assembleegameclient/parameters/Parameters.as').read_text()
keycodes_as = (FLASH / 'util/KeyCodes.as').read_text()
native = json.loads(subprocess.run([str(ROOT / 'build/realm_options_inputs'), '--table'], check=True, capture_output=True, text=True).stdout)
codes = {name: int(value) for name, value in re.findall(r'static const (\w+):uint = (\d+);', keycodes_as)}
TABS = ['CONTROLS', 'GAMEPLAY', 'HOTKEYS', 'CHAT', 'GRAPHICS', 'SOUND']
# Rows Flash adds that the native client deliberately omits.
EXPECTED = {'GPURender'}  # "Hardware Acceleration" appears only while the Flash GPU path can fail over

def string(text):
    """Concatenated AS string literals ("a" + "b") with their escapes."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', text)
    return ''.join(p.replace("\\'", "'").replace('\\"', '"').replace('\\\\', '\\') for p in parts)

def function_body(name):
    m = re.search(r'function ' + name + r'\(\) : void\s*\{', options_as)
    depth, i = 1, m.end()
    while depth:
        depth += {'{': 1, '}': -1}.get(options_as[i], 0); i += 1
    return options_as[m.end():i]

def rows(body):
    result = []
    for m in re.finditer(r'this\.addOption\((.*?)\);\s*$', body, re.M | re.S):
        text = m.group(1)
        if text.strip() == 'new Sprite()':
            result.append(dict(kind='spacer')); continue
        km = re.match(r'new KeyMapper\("(\w+)",\s*"([^"]*)",\s*(.*?)(?:,(!Parameters\.data_\.\w+))?\)\s*$', text, re.S)
        if km:
            result.append(dict(kind='key', param=km.group(1), label=km.group(2), tooltip=string(km.group(3)), enabled_by=(km.group(4) or '').replace('!Parameters.data_.', '')))
            continue
        cm = re.match(r'new ChoiceOption\("(\w+)",\s*new <String>\[(.*?)\],\s*\[(.*?)\],\s*"([^"]*)",\s*(.*)\)\s*$', text, re.S)
        assert cm, text
        tooltip = string(cm.group(5).rsplit(',', 1)[0])
        result.append(dict(kind='choice', param=cm.group(1), labels=re.findall(r'"([^"]*)"', cm.group(2)), values=[v.strip() for v in cm.group(3).split(',')],
                           label=cm.group(4), tooltip=tooltip))
    return result

def defaults():
    result = {}
    for name, value in re.findall(r'setDefaultKey\("(\w+)",\s*KeyCodes\.(\w+)\)', parameters_as):
        result[name] = codes[value]
    for name, value in re.findall(r'setDefault\("(\w+)",\s*([^)]*?)\)', parameters_as):
        result[name] = value.strip()
    return result

flash_defaults = defaults()
by_tab = {i: [r for r in native if r['tab'] == i and r['kind'] in (0, 1, 2)] for i in range(6)}
checked = 0
for index, (tab, function) in enumerate(zip(TABS, ['addControlsOptions', 'addGameplayOptions', 'addHotKeysOptions', 'addChatOptions', 'addGraphicsOptions', 'addSoundOptions'])):
    flash_rows = [r for r in rows(function_body(function)) if r.get('param') not in EXPECTED]
    native_rows = by_tab[{'CONTROLS': 0, 'GAMEPLAY': 1, 'HOTKEYS': 2, 'CHAT': 3, 'GRAPHICS': 4, 'SOUND': 5}[tab]]
    assert len(flash_rows) == len(native_rows), (tab, len(flash_rows), len(native_rows))
    for fr, nr in zip(flash_rows, native_rows):
        if fr['kind'] == 'spacer':
            assert nr['kind'] == 2, (tab, 'spacer position', nr); checked += 1; continue
        assert nr['param'] == fr['param'], (tab, nr['param'], fr['param'])
        assert nr['label'] == fr['label'], (nr['param'], 'label', nr['label'], fr['label'])
        assert nr['tooltip'] == fr['tooltip'], (nr['param'], 'tooltip', nr['tooltip'], fr['tooltip'])
        assert nr['enabled_by'] == fr.get('enabled_by', ''), (nr['param'], 'disabled by', nr['enabled_by'])
        default = flash_defaults[fr['param']]
        if fr['kind'] == 'key':
            assert nr['kind'] == 0 and nr['default'] == default, (nr['param'], 'default key', nr['default'], default)
        else:
            assert nr['kind'] == 1 and [c[0] for c in nr['choices']] == fr['labels'], (nr['param'], 'choice labels', nr['choices'], fr['labels'])
            native_values = [c[1] for c in nr['choices']]
            wanted = []
            for v in fr['values']:
                wanted.append({'true': 1, 'false': 0, 'MouseCursor.AUTO': -1, '7 * Math.PI / 4': 1, '0': 0}.get(v, None))
            if fr['param'] == 'defaultCameraAngle':
                assert native_values == [1, 0]
            elif fr['param'] == 'cursor':
                assert native_values == [-1] + list(range(16))
            else:
                ints = [int(v) if v.lstrip('-').isdigit() else wanted[i] for i, v in enumerate(fr['values'])]
                assert native_values == ints, (nr['param'], native_values, ints)
            if fr['param'] == 'cursor': assert nr['default'] == int(default.strip('"'))
            elif fr['param'] == 'defaultCameraAngle': assert nr['default'] == 1
            else:
                expect = {'true': 1, 'false': 0}.get(str(default), None)
                if expect is None: expect = int(default)
                assert nr['default'] == expect, (nr['param'], 'default value', nr['default'], default)
        checked += 1
# Rows without a UI entry, but registered as bindings or defaults.
hidden = {r['param']: r for r in native if r['kind'] in (3, 4) or r['param'] in ('escapeToNexus2', 'showTradePopup')}
assert hidden['escapeToNexus2']['default'] == flash_defaults['escapeToNexus2'] == 116 and hidden['showTradePopup']['default'] == 1
# Every key registered with setDefaultKey is in the table (Parameters.setKey clears them on conflict).
flash_keys = set(re.findall(r'setDefaultKey\("(\w+)"', parameters_as))
native_keys = {r['param'] for r in native if r['kind'] in (0, 3)}
assert flash_keys == native_keys, (flash_keys ^ native_keys)
print(f'{checked} Options.as rows and {len(flash_keys)} Parameters key defaults match the native table')
