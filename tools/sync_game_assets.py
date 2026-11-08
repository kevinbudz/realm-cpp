#!/usr/bin/env python3
"""Vendor the runtime game assets of a realm-client checkout into assets/, with manifest.json.

This is the only script that reads the AS3 sources. It reads the registrations (EmbeddedAssets.as and AssetLoader.as
for image sets, animated characters and OBJ models; EmbeddedData.as for the ground, object and region XML;
GameServerConnection.as and StatData.as for the packet and stat IDs), so the build never needs realm-client.

Writes, under --output (default assets/):
  images/  every PNG under src/. All of them are asset-table entries, in sorted original-path order.
  models/  the OBJ models that EmbeddedAssets.models_ registers (build-time input of tools/compile_models.py).
  xml/     the XML definitions the client loads: EmbeddedData XML, particles, skins, tutorial script, regions.
  fonts/   the two Myriad OTF fonts.
  shapes/  the SWF shapes.
  data/    MapBackground.jm, the Startup background map (JSON, read as a map).
  manifest.json  the registrations, the realm-client revision and dirty flag, and each vendored file's original Flash
                 path and sha256.

Names: a file the AS3 wrappers embed is named from its wrapper class, with the EmbeddedAssets_/EmbeddedData_ prefix
and the Embed_, Embed, CXML and trailing _ suffixes removed (EmbeddedAssets_lofiObjEmbed_ -> lofiObj.png). Other files
are named from their own stem with the same suffixes removed, and Png too (KeysView_greenKeyPng.png -> KeysView_greenKey.png).
Two files that map to one name (ignoring case) stop the sync.

A .dat that is neither OBJ nor XML must be listed in OTHER_DATA_FILES, or the sync stops. Each entry is either a
target (kept) or None (dropped, with the reason).

Removes assets/flash/ and assets/sfx/ (the earlier layout) and stale files in the folders above. Unchanged files are
not rewritten, so builds stay incremental.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[1]
FOLDERS = ('images', 'models', 'xml', 'fonts', 'shapes', 'data')
ASSET_DIR = 'src/kabam/rotmg/assets'
FIXED_AS = ['src/com/company/assembleegameclient/util/AssetLoader.as',
            'src/kabam/rotmg/assets/EmbeddedAssets.as',
            'src/kabam/rotmg/assets/EmbeddedData.as',
            'src/kabam/rotmg/messaging/impl/GameServerConnection.as',
            'src/kabam/rotmg/messaging/impl/data/StatData.as']
# Non-XML, non-OBJ .dat files. Target: (folder, file name), or None to drop the file.
OTHER_DATA_FILES = {
    'src/kabam/rotmg/ui/view/components/MapBackground_EMBEDDED_BACKGROUNDMAP.dat': ('data', 'MapBackground.jm'),
    # Pixel Bender shader bytecode for the Flash TextureRedrawer. The native client has no shader path and nothing reads it.
    'src/com/company/assembleegameclient/util/TextureRedrawer_textureShaderEmbed_.dat': None,
}
FOLDER_OF_SUFFIX = {'.png': ('images', '.png'), '.otf': ('fonts', '.otf'), '.swf': ('shapes', '.swf')}
NAME_SUFFIXES = ('Embed_', 'Embed', 'CXML', 'Png', '_')
CLASS_PREFIXES = ('EmbeddedAssets_', 'EmbeddedData_')


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else 'unavailable'


def source_identity(root):
    """Revision, dirty flag, digest and .as count of the checkout (git metadata and every file under src/)."""
    source_files = sorted(p for p in (root / 'src').rglob('*') if p.is_file())
    digest = hashlib.sha256()
    for p in source_files:
        digest.update(p.relative_to(root).as_posix().encode())
        digest.update(b'\0')
        digest.update(hashlib.sha256(p.read_bytes()).digest())
    return dict(revision=git(root, 'rev-parse', 'HEAD'), dirty=git(root, 'status', '--porcelain') != '',
                source_sha256=digest.hexdigest(), actionscript_files=sum(p.suffix == '.as' for p in source_files))


def embed_source(wrapper):
    match = re.search(r'\[Embed\(source="([^"]+)"', wrapper.read_text())
    if not match:
        raise ValueError(f'Missing Embed source: {wrapper}')
    return wrapper.parent / match[1]


def rel(root, path):
    """Path relative to the checkout root, in POSIX form; the wrapper's embed path must resolve under it."""
    return path.resolve().relative_to(root).as_posix()


def strip_suffixes(text, suffixes):
    changed = True
    while changed:
        changed = False
        for suffix in suffixes:
            if text.endswith(suffix) and len(text) > len(suffix):
                text = text[:-len(suffix)]
                changed = True
                break
    return text


def class_name(cls):
    for prefix in CLASS_PREFIXES:
        if cls.startswith(prefix):
            cls = cls[len(prefix):]
            break
    return strip_suffixes(cls, NAME_SUFFIXES)


def stem_name(stem):
    return strip_suffixes(stem, NAME_SUFFIXES)


def scan(root):
    """The registrations, read the way the generators read them before (see the comments in each step)."""
    src = root / 'src'
    asset_dir = root / ASSET_DIR
    loader = (src / 'com/company/assembleegameclient/util/AssetLoader.as').read_text()
    embedded = (asset_dir / 'EmbeddedAssets.as').read_text()
    data_text = (asset_dir / 'EmbeddedData.as').read_text()
    fields = dict(re.findall(r'(\w+):Class\s*=\s*(\w+)\s*;', embedded))
    data_fields = dict(re.findall(r'(\w+):Class\s*=\s*(\w+)\s*;', data_text))

    # Every PNG under src/ is an asset-table entry, in sorted original-path order (sheet indices depend on it).
    pngs = sorted(src.rglob('*.png'))
    png_index = {}
    png_size = {}
    for p in pngs:
        data = p.read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
            raise ValueError(f'Invalid PNG: {p}')
        png_index[rel(root, p)] = len(png_index)
        png_size[rel(root, p)] = struct.unpack('>II', data[16:24])

    def wrapper_file(cls):
        wrapper = asset_dir / (cls + '.as')
        if not wrapper.is_file():
            raise FileNotFoundError(f'Wrapper for {cls} is missing: {wrapper}')
        return rel(root, embed_source(wrapper))

    def resolve(field):
        target = wrapper_file(fields[field])
        if target not in png_index:
            raise ValueError(f'{field} does not embed a PNG: {target}')
        return png_index[target]

    sheets = []
    for name, field, w, h in re.findall(
            r'AssetLibrary\.addImageSet\("([^"]+)",new EmbeddedAssets\.(\w+)\(\)\.bitmapData,(\d+),(\d+)\)', loader):
        w, h = int(w), int(h)
        sheets.append(dict(name=name, asset=resolve(field), width=w, height=h))
    if 'AssetLibrary.addImageSet("invisible",new BitmapData(8,8,true,0),8,8)' in loader:
        sheets.append(dict(name='invisible', asset=-1, width=8, height=8))
    if not sheets:
        raise ValueError('No image set registrations found; AssetLoader may have changed')
    animated = []
    for name, field, mask, w, h, sw, sh, direction in re.findall(
            r'AnimatedChars\.add\("([^"]+)",new EmbeddedAssets\.(\w+)\(\)\.bitmapData,'
            r'(null|new EmbeddedAssets\.\w+\(\)\.bitmapData),(\d+),(\d+),(\d+),(\d+),AnimatedChar\.(RIGHT|DOWN)\)', loader):
        mask_field = re.search(r'EmbeddedAssets\.(\w+)', mask)
        animated.append(dict(name=name, asset=resolve(field), mask=resolve(mask_field[1]) if mask_field else -1,
                             width=int(w), height=int(h), strip_width=int(sw), strip_height=int(sh), first=direction))

    connection = (src / 'kabam/rotmg/messaging/impl/GameServerConnection.as').read_text()
    packets = {name: int(value) for name, value in re.findall(r'public static const (\w+):int\s*=\s*(\d+);', connection)}
    stat_file = (src / 'kabam/rotmg/messaging/impl/data/StatData.as').read_text()
    stats = {name: int(value) for name, value in re.findall(r'public static const (\w+):int\s*=\s*(\d+);', stat_file)}
    is_string = re.search(r'function isStringStat\(\).*?\n\s*}', stat_file, re.S)
    if not is_string:
        raise ValueError('String stat discriminator not found')
    string_stats = [stats[name] for name in re.findall(r'case (\w+):', is_string[0])]

    data_files = []
    for kind in ['groundFiles', 'objectFiles', 'regionFiles']:
        declaration = re.search(rf'{kind}:Array\s*=\s*\[([^\]]*)\]', data_text)
        if not declaration:
            raise ValueError(f'Missing data registration: {kind}')
        for field in re.findall(r'new (\w+)\(\)', declaration[1]):
            data_files.append(dict(kind=kind, original=wrapper_file(data_fields[field])))

    registry = re.search(r'models_:Object\s*=\s*\{(.*?)\n\s*\};', embedded, re.S)
    if not registry:
        raise ValueError('EmbeddedAssets.models_ was not found')
    models = [dict(name=name, original=wrapper_file(fields[field]))
              for name, field in re.findall(r'"([^"]+)"\s*:\s*new (\w+)\(\)', registry[1])]
    if not models:
        raise ValueError('EmbeddedAssets.models_ has no entries')

    # Every wrapper class and the file it embeds, for naming. A file embedded by several classes takes the first name.
    classes = {}
    for text in (embedded, data_text):
        for cls in re.findall(r'\w+:Class\s*=\s*(\w+)\s*;', text):
            wrapper = asset_dir / (cls + '.as')
            if wrapper.is_file():
                classes.setdefault(rel(root, embed_source(wrapper)), set()).add(cls)
    return dict(pngs=[rel(root, p) for p in pngs], png_index=png_index, png_size=png_size, sheets=sheets,
                animated=animated, packets=packets, stats=stats, string_stats=string_stats,
                data_files=data_files, models=models, classes=classes)


def classify(root, reg):
    """Maps every file under src/ that the client or a generator uses to (folder, file name, original path)."""
    src = root / 'src'
    unknown = sorted({p.suffix for p in src.rglob('*') if p.is_file()} - {'.as', '.png', '.dat', '.otf', '.swf'})
    if unknown:
        raise ValueError(f'Unexpected file types under src/: {unknown}; add them to tools/sync_game_assets.py')
    model_files = {m['original'] for m in reg['models']}
    targets = {}
    dropped = []
    for path in sorted(src.rglob('*')):
        if not path.is_file() or path.suffix == '.as':
            continue
        original = rel(root, path)
        if original in OTHER_DATA_FILES:
            if OTHER_DATA_FILES[original] is None:
                dropped.append(original)
                continue
            folder, name = OTHER_DATA_FILES[original]
        elif path.suffix == '.dat':
            if original in model_files:
                folder, ext = 'models', '.obj'
            else:
                try:
                    ET.fromstring(path.read_bytes())
                except ET.ParseError as error:
                    raise ValueError(f'{original} is neither OBJ, XML nor listed in OTHER_DATA_FILES: {error}')
                folder, ext = 'xml', '.xml'
            name = None
        else:
            folder, ext = FOLDER_OF_SUFFIX[path.suffix]
            name = None
        if name is None:
            if original in reg['classes']:
                short = class_name(sorted(reg['classes'][original])[0])
            else:
                short = stem_name(path.stem)
            name = short + ext
        targets[original] = (folder, name)
    return targets, dropped


def plan_files(root, reg):
    """Relative target path -> original path, for every vendored file. Stops on a name collision."""
    targets, dropped = classify(root, reg)
    claimed = {}
    plan = {}
    for original, (folder, name) in sorted(targets.items()):
        target = f'{folder}/{name}'
        key = target.lower()
        if key in claimed:
            raise ValueError(f'Name collision: {target} from {claimed[key]} and {original}')
        claimed[key] = original
        plan[target] = original
    # Every registered PNG must be in the table; a dropped file must not be used by any registration.
    for png in reg['pngs']:
        if targets.get(png, (None,))[0] != 'images':
            raise ValueError(f'PNG not vendored: {png}')
    for entry in reg['data_files'] + reg['models']:
        if entry['original'] not in targets:
            raise ValueError(f"Registered file was not vendored: {entry['original']}")
    return plan, dropped, targets


def write_if_changed(path, data):
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def mirror(root, plan, dest):
    """Copies the planned files into dest and deletes every other file in the owned folders. Returns (copied, removed)."""
    keep = set()
    copied = 0
    for target, original in sorted(plan.items()):
        keep.add(dest / target)
        copied += write_if_changed(dest / target, (root / original).read_bytes())
    removed = 0
    for folder in FOLDERS:
        base = dest / folder
        if base.is_dir():
            for path in sorted(base.rglob('*')):
                if path.is_file() and path not in keep:
                    path.unlink()
                    removed += 1
            for path in sorted(base.rglob('*'), reverse=True):
                if path.is_dir() and not any(path.iterdir()):
                    path.rmdir()
    return copied, removed


def build_manifest(root, identity, reg, plan, dest):
    file_list = []
    for target, original in sorted(plan.items()):
        data = (root / original).read_bytes()
        file_list.append(dict(file=target, original=original, sha256=hashlib.sha256(data).hexdigest()))
    target_of = {original: target for target, original in plan.items()}
    images = [dict(file=target_of[p], width=reg['png_size'][p][0], height=reg['png_size'][p][1]) for p in reg['pngs']]
    return dict(
        schema=1,
        source=dict(revision=identity['revision'], dirty=identity['dirty'],
                    source_sha256=identity['source_sha256'], actionscript_files=identity['actionscript_files']),
        images=images,
        sheets=[dict(name=s['name'], asset=s['asset'], width=s['width'], height=s['height'],
                     count=(images[s['asset']]['width'] // s['width']) * (images[s['asset']]['height'] // s['height'])
                     if s['asset'] >= 0 else 1) for s in reg['sheets']],
        animated=reg['animated'],
        packets=reg['packets'], stats=reg['stats'], string_stats=reg['string_stats'],
        data_files=[dict(kind=d['kind'], file=target_of[d['original']]) for d in reg['data_files']],
        models=[dict(name=m['name'], file=target_of[m['original']]) for m in reg['models']],
        files=file_list,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--flash-root', type=Path, default=REPO.parent / 'realm-client',
                        help='realm-client checkout (default: ../realm-client)')
    parser.add_argument('--output', type=Path, default=REPO / 'assets',
                        help='assets directory to write (default: assets/)')
    args = parser.parse_args()
    root = args.flash_root.resolve()
    dest = args.output.resolve()
    if not (root / 'src').is_dir():
        raise SystemExit(f'realm-client source not found: {root}')
    for name in FIXED_AS:
        if not (root / name).is_file():
            raise SystemExit(f'realm-client file not found: {root / name}')
    identity = source_identity(root)
    if identity['revision'] == 'unavailable':
        raise SystemExit(f'{root} is not a git checkout, so its revision cannot be recorded')

    reg = scan(root)
    plan, dropped, _ = plan_files(root, reg)
    manifest = build_manifest(root, identity, reg, plan, dest)

    # The earlier layout is replaced entirely.
    for old in ('flash', 'sfx'):
        if (dest / old).is_dir():
            shutil.rmtree(dest / old)
    copied, removed = mirror(root, plan, dest)
    text = json.dumps(manifest, indent=2) + '\n'
    manifest_changed = write_if_changed(dest / 'manifest.json', text.encode())
    counts = {folder: sum(t.startswith(folder + '/') for t in plan) for folder in FOLDERS}
    print(f"realm-client {identity['revision'][:12]}{' (dirty)' if identity['dirty'] else ''}: "
          + ', '.join(f'{folder} {counts[folder]}' for folder in FOLDERS)
          + f'; {copied} copied, {removed} removed; manifest {"written" if manifest_changed else "unchanged"}')
    for original in dropped:
        print(f'dropped (no runtime or generator use): {original}', file=sys.stderr)


if __name__ == '__main__':
    main()
