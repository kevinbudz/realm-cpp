#!/usr/bin/env python3
"""Vendor the Flash and SFX files that realm_client reads into assets/, where CMake embeds them.

Sources: --flash-root (a realm-client git checkout) and --sfx-root (realm-server's Resources/Web/sfx).
Copied to assets/flash/, paths kept relative to the Flash root:
  * every *.png, *.dat, *.otf and *.swf under src/ (the runtime art and XML, and the OBJ models that
    tools/compile_models.py reads at build time);
  * only the .as files that tools/import_flash.py and tools/compile_models.py open (generator_as_files).
Copied to assets/sfx/, recursively, as the web build preloaded them: every *.mp3.
Also writes assets/flash/SOURCE.json with the revision, dirty flag, digest and .as count of the full Flash
checkout. import_flash.py prefers that file, so the generated headers keep the identity of the live tree.
Files that are no longer needed are removed. Unchanged files are not rewritten, so builds stay incremental.
"""
import argparse
import json
from pathlib import Path
import re

import import_flash

REPO = Path(__file__).resolve().parents[1]
RUNTIME_SUFFIXES = {'.png', '.dat', '.otf', '.swf'}
ASSET_DIR = 'src/kabam/rotmg/assets'
# Read by import_flash.py and compile_models.py whatever the registrations are.
FIXED_AS = ['src/com/company/assembleegameclient/util/AssetLoader.as',
            'src/kabam/rotmg/assets/EmbeddedAssets.as',
            'src/kabam/rotmg/assets/EmbeddedData.as',
            'src/kabam/rotmg/messaging/impl/GameServerConnection.as',
            'src/kabam/rotmg/messaging/impl/data/StatData.as']


def generator_as_files(root):
    """Relative paths of the .as files the generators open. Mirrors their patterns: a wrapper that is missing from
    assets/flash makes the build-time import fail, so any drift shows up at configure time."""
    asset_dir = root / ASSET_DIR
    loader = (root / 'src/com/company/assembleegameclient/util/AssetLoader.as').read_text()
    embedded = (asset_dir / 'EmbeddedAssets.as').read_text()
    data = (asset_dir / 'EmbeddedData.as').read_text()
    classes = dict(re.findall(r'(\w+):Class\s*=\s*(\w+)\s*;', embedded))
    data_classes = dict(re.findall(r'(\w+):Class\s*=\s*(\w+)\s*;', data))
    # import_flash.scan: image sets, animated sets with their masks, then the models (compile_models.py).
    fields = {field for _, field, _, _ in re.findall(
        r'AssetLibrary\.addImageSet\("([^"]+)",new EmbeddedAssets\.(\w+)\(\)\.bitmapData,(\d+),(\d+)\)', loader)}
    for match in re.finditer(
            r'AnimatedChars\.add\("([^"]+)",new EmbeddedAssets\.(\w+)\(\)\.bitmapData,'
            r'(null|new EmbeddedAssets\.\w+\(\)\.bitmapData),(\d+),(\d+),(\d+),(\d+),AnimatedChar\.(RIGHT|DOWN)\)', loader):
        fields.add(match[2])
        mask = re.search(r'EmbeddedAssets\.(\w+)', match[3])
        if mask:
            fields.add(mask[1])
    registry = re.search(r'models_:Object\s*=\s*\{(.*?)\n\s*\};', embedded, re.S)
    if not registry:
        raise ValueError('EmbeddedAssets.models_ was not found')
    fields |= {field for _, field in re.findall(r'"([^"]+)"\s*:\s*new (\w+)\(\)', registry[1])}
    paths = {asset_dir / (classes[field] + '.as') for field in fields}
    # import_flash.scan: the data registrations (groundFiles, objectFiles, regionFiles) and their wrappers.
    for kind in ['groundFiles', 'objectFiles', 'regionFiles']:
        declaration = re.search(rf'{kind}:Array\s*=\s*\[([^\]]*)\]', data)
        if not declaration:
            raise ValueError(f'Missing data registration: {kind}')
        paths |= {asset_dir / (data_classes[field] + '.as') for field in re.findall(r'new (\w+)\(\)', declaration[1])}
    paths |= {root / rel for rel in FIXED_AS}
    missing = sorted(p for p in paths if not p.is_file())
    if missing:
        raise FileNotFoundError(f'Generator input missing from the Flash root: {missing[0]}')
    return sorted(p.relative_to(root).as_posix() for p in paths)


def flash_plan(root):
    """Relative path -> source file for everything assets/flash holds."""
    plan = {p.relative_to(root).as_posix(): p
            for p in (root / 'src').rglob('*') if p.is_file() and p.suffix in RUNTIME_SUFFIXES}
    plan.update({rel: root / rel for rel in generator_as_files(root)})
    return plan


def sfx_plan(root):
    return {p.relative_to(root).as_posix(): p for p in root.rglob('*.mp3') if p.is_file()}


def write_if_changed(path, data):
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def mirror(plan, dest, keep_extra=()):
    """Copies plan into dest and deletes every other file there. Returns (copied, removed) counts."""
    keep = {dest / name for name in keep_extra}
    copied = 0
    for rel, source in sorted(plan.items()):
        keep.add(dest / rel)
        copied += write_if_changed(dest / rel, source.read_bytes())
    removed = 0
    if dest.is_dir():
        for path in sorted(dest.rglob('*')):
            if path.is_file() and path not in keep:
                path.unlink()
                removed += 1
        for path in sorted(dest.rglob('*'), reverse=True):
            if path.is_dir() and not any(path.iterdir()):
                path.rmdir()
    return copied, removed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--flash-root', type=Path, default=REPO.parent / 'realm-client',
                        help='realm-client checkout (default: ../realm-client)')
    parser.add_argument('--sfx-root', type=Path, default=REPO.parent / 'realm-server/Resources/Web/sfx',
                        help='SFX directory (default: ../realm-server/Resources/Web/sfx)')
    parser.add_argument('--dest', type=Path, default=REPO / 'assets',
                        help='directory holding flash/ and sfx/ (default: assets/)')
    args = parser.parse_args()
    flash_root = args.flash_root.resolve()
    sfx_root = args.sfx_root.resolve()
    if not (flash_root / 'src').is_dir():
        raise SystemExit(f'Flash source not found: {flash_root}')
    if not sfx_root.is_dir():
        raise SystemExit(f'SFX directory not found: {sfx_root}')
    identity = import_flash.tree_identity(flash_root)
    if identity['revision'] == 'unavailable':
        raise SystemExit(f'{flash_root} is not a git checkout, so its revision cannot be recorded')

    flash_dest = args.dest.resolve() / 'flash'
    sfx_dest = args.dest.resolve() / 'sfx'
    flash = flash_plan(flash_root)
    copied, removed = mirror(flash, flash_dest, keep_extra=['SOURCE.json'])
    source = json.dumps(identity, indent=2) + '\n'
    write_if_changed(flash_dest / 'SOURCE.json', source.encode())
    sfx = sfx_plan(sfx_root)
    sfx_copied, sfx_removed = mirror(sfx, sfx_dest)
    as_count = sum(rel.endswith('.as') for rel in flash)
    print(f"flash {identity['revision'][:12]}{' (dirty)' if identity['dirty'] else ''}: {len(flash) - as_count} runtime "
          f"files, {as_count} .as files; {copied} copied, {removed} removed")
    print(f"sfx: {len(sfx)} mp3 files; {sfx_copied} copied, {sfx_removed} removed")


if __name__ == '__main__':
    main()
