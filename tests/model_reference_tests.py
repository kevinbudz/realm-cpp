#!/usr/bin/env python3
"""Real Flash GameObject/Object3D frames against the native Scene/OpenGL model path.

Every <Model> name in the registered object XML is drawn at normal and rotated
camera angles; selected models are also drawn at 2x, with explicit object
rotations, and in an overlapping painter-order scene. Flash is the CPU
(Object3D/ObjectFace3D) path; native is the Stage3D-equivalent mesh renderer.
"""
import json, subprocess, sys, xml.etree.ElementTree as ET
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build/model-reference'
source = ROOT.parent / 'realm-client/src/kabam/rotmg/assets'
out.mkdir(parents=True, exist_ok=True)

# One representative object per model name, in source order.
representative = {}
for path in sorted(source.glob('EmbeddedData_*CXML.dat')):
    for obj in ET.fromstring(path.read_bytes()).findall('Object'):
        model = obj.findtext('Model')
        if model and model not in representative:
            representative[model] = obj.get('id')
assert len(representative) >= 30, representative

cases = []
def add(name, items, scale=1, angle=0.0):
    cases.append(dict(id=name, scale=scale, angle=angle, items=items))
for index, (model, object_id) in enumerate(sorted(representative.items())):
    if object_id == 'Spider Web':
        continue
    slug = f'{index:02}-' + ''.join(c if c.isalnum() else '_' for c in object_id)
    item = [dict(object=object_id, x=50.5, y=50.5)]
    add(f'model-{slug}-a0', item)
    add(f'model-{slug}-a1', item, angle=0.6)
spot = ['Cube', 'Table', 'Pyramid', 'Tower', 'Gate', 'Monument 1']
spot = [representative[m] for m in ('Cube', 'Table', 'Pyramid', 'Tower', 'Gate', 'Monument 1') if m in representative]
for object_id in spot:
    slug = ''.join(c if c.isalnum() else '_' for c in object_id)
    add(f'zoom-{slug}', [dict(object=object_id, x=50.5, y=50.5)], scale=2)
    for rotation in (30, 90, 200):
        add(f'rotate-{slug}-{rotation}', [dict(object=object_id, x=50.5, y=50.5, rotation=rotation)])
# SpiderWeb draws only beside a Wall and turns away from it: west keeps XML rotation, north 90, east 180, south 270.
web = representative['Web']
assert web == 'Spider Web', web
for name, wall in (('west', (49, 50)), ('north', (50, 49)), ('east', (51, 50)), ('south', (50, 51))):
    cases.append(dict(id=f'web-{name}', scale=1, angle=0.0, items=[dict(object=web, x=50.5, y=50.5)], walls=[dict(object='Grey Wall', x=wall[0], y=wall[1])]))
cases.append(dict(id='web-first-match', scale=1, angle=0.0, items=[dict(object=web, x=50.5, y=50.5)], walls=[dict(object='Grey Wall', x=51, y=50), dict(object='Grey Wall', x=50, y=49)]))
cases.append(dict(id='web-alone', scale=1, angle=0.0, items=[dict(object=web, x=50.5, y=50.5), dict(object=representative['Cube'], x=52.5, y=50.5)]))
if 'Cube' in representative and 'Pillar' in representative:
    cube, pillar = representative['Cube'], representative['Pillar']
    add('overlap-order', [dict(object=pillar, x=50.5, y=50.5), dict(object=cube, x=50.5, y=51.0),
                          dict(object=cube, x=51.2, y=50.4), dict(object=pillar, x=49.7, y=51.2)])
    add('overlap-order-camera', [dict(object=pillar, x=50.5, y=50.5), dict(object=cube, x=50.5, y=51.0),
                                 dict(object=cube, x=51.2, y=50.4), dict(object=pillar, x=49.7, y=51.2)], angle=0.9)

(out / 'input.json').write_text(json.dumps(dict(output=str(out / 'flash'), cases=cases)))
if '--reuse' not in sys.argv:
    subprocess.run([sys.executable, str(ROOT / 'tools/build_flash_reference.py'), 'ModelsExporter', '--output', str(out / 'exporter')],
                   check=True, cwd=ROOT, stdout=(out / 'compile.log').open('w'), stderr=subprocess.STDOUT)
    subprocess.run([str(Path.home() / 'Desktop/AIRSDK_Linux/bin/adl'), '-nodebug', str(out / 'exporter/ModelsExporter-app.xml'), '--', str(out / 'input.json')],
                   check=True, timeout=180, cwd=ROOT)
subprocess.run([str(ROOT / 'build/realm_model_inputs'), str(out / 'flash/states.json'), str(out / 'native')], check=True, cwd=ROOT)

background = (54, 54, 54)
rows, empty = [], []
contact = Image.new('RGB', (1200, ((len(cases) + 3) // 4) * 175), (40, 40, 40)); draw = ImageDraw.Draw(contact)
for i, c in enumerate(cases):
    f = Image.open(out / 'flash' / (c['id'] + '.png')).convert('RGB')
    n = Image.open(out / 'native' / (c['id'] + '.png')).convert('RGB')
    assert f.size == n.size, c['id']
    # Faint antialiasing pixels stay visible in the comparison images but do not decide placement.
    solid = lambda im: ImageChops.difference(im, Image.new('RGB', im.size, background)).convert('L').point(lambda v: 255 if v > 16 else 0).getbbox()
    fb, nb = solid(f), solid(n)
    if not fb and not nb:
        # Edge-on thin plates (Gate at rotation 0) project to under a pixel in both renderers.
        empty.append(c['id']); rows.append((c['id'], 0, 0.0)); continue
    assert fb, f"Flash drew nothing for {c['id']}, native {nb}"
    assert nb, f"native drew nothing for {c['id']}, Flash {fb}"
    edge = max(abs(a - b) for a, b in zip(fb, nb))
    # Fraction of pixels that differ materially inside the union of both silhouettes.
    box = (min(fb[0], nb[0]), min(fb[1], nb[1]), max(fb[2], nb[2]), max(fb[3], nb[3]))
    diff = ImageChops.difference(f.crop(box), n.crop(box)).convert('L').point(lambda v: 255 if v > 48 else 0)
    area = (box[2] - box[0]) * (box[3] - box[1])
    changed = sum(1 for v in diff.get_flattened_data() if v) / area
    rows.append((c['id'], edge, changed))
    pair = Image.new('RGB', (f.width * 2, f.height + 22), (40, 40, 40)); pair.paste(f, (0, 22)); pair.paste(n, (f.width, 22))
    ImageDraw.Draw(pair).text((5, 3), c['id'] + ' Flash / native', fill='white'); pair.save(out / ('comparison-' + c['id'] + '.png'))
    crop = pair.crop((120 * c['scale'], 22 + 90 * c['scale'], 600 * c['scale'] + f.width, 22 + 510 * c['scale'])); crop.thumbnail((298, 150))
    contact.paste(crop, ((i % 4) * 300, (i // 4) * 175 + 20)); draw.text(((i % 4) * 300 + 3, (i // 4) * 175 + 2), c['id'][:48], fill='white')
contact.save(out / 'contact.png')
limit = 3
# Zoom doubles pixel deltas. Gates are 0.016-unit plates under a pixel wide: rasterizer coverage may move their ends.
tolerance = lambda name: limit * (2 if name.startswith('zoom') else 1) if 'Gate' not in name else 6
bad = [r for r in rows if r[1] > tolerance(r[0])]
for name, edge, changed in sorted(rows, key=lambda r: -r[2])[:12]:
    print(f'{name:48} silhouette edge delta {edge:3}px  materially differing {changed * 100:5.1f}%')
print(f'{len(rows)} actual Flash model-class/native Scene frames; {len(bad)} silhouette deltas over {limit}px; edge-on empty in both: {empty}')
assert not bad, bad
