#!/usr/bin/env python3
"""Generate native asset metadata from the authoritative AS3 registrations.

No image resampling, asset substitution, or guessed protocol IDs. Only the
registrations actually used by AssetLoader are exposed as image sets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET


def git(root, *args):
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unavailable"


def scan(root):
    src = root / "src"
    asset_dir = src / "kabam/rotmg/assets"
    asset_text = (asset_dir / "EmbeddedAssets.as").read_text()
    loader = (src / "com/company/assembleegameclient/util/AssetLoader.as").read_text()
    fields = dict(re.findall(r'(\w+):Class\s*=\s*(\w+)\s*;', asset_text))
    assets, indices = [], {}
    for p in sorted(src.rglob("*.png")):
        data = p.read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR':
            raise ValueError(f"Invalid PNG: {p}")
        w, h = struct.unpack(">II", data[16:24])
        relative = p.relative_to(root).as_posix()
        indices[relative] = len(assets)
        assets.append(dict(path=relative, width=w, height=h, sha256=hashlib.sha256(data).hexdigest()))

    def resolve(field):
        wrapper = asset_dir / (fields[field] + ".as")
        match = re.search(r'\[Embed\(source="([^"]+)"', wrapper.read_text())
        if not match:
            raise ValueError(f"Missing Embed source: {wrapper}")
        relative = (wrapper.parent / match[1]).relative_to(root).as_posix()
        return indices[relative]

    sheets = []
    for name, field, w, h in re.findall(
        r'AssetLibrary\.addImageSet\("([^"]+)",new EmbeddedAssets\.(\w+)\(\)\.bitmapData,(\d+),(\d+)\)', loader
    ):
        w, h = int(w), int(h)
        asset = resolve(field)
        sheets.append(dict(name=name, asset=asset, width=w, height=h,
                           count=(assets[asset]["width"] // w) * (assets[asset]["height"] // h)))
    # AssetLoader synthesizes this transparent image rather than embedding it.
    if 'AssetLibrary.addImageSet("invisible",new BitmapData(8,8,true,0),8,8)' in loader:
        sheets.append(dict(name="invisible", asset=-1, width=8, height=8, count=1))
    if not sheets:
        raise ValueError("No image set registrations found; AssetLoader may have changed")
    animated = []
    for name, field, mask, w, h, sw, sh, direction in re.findall(
        r'AnimatedChars\.add\("([^"]+)",new EmbeddedAssets\.(\w+)\(\)\.bitmapData,'
        r'(null|new EmbeddedAssets\.\w+\(\)\.bitmapData),(\d+),(\d+),(\d+),(\d+),AnimatedChar\.(RIGHT|DOWN)\)', loader
    ):
        mask_field = re.search(r'EmbeddedAssets\.(\w+)', mask)
        animated.append(dict(name=name, asset=resolve(field), mask=resolve(mask_field[1]) if mask_field else -1,
                             width=int(w), height=int(h), strip_width=int(sw), strip_height=int(sh), first=direction))

    connection = (src / "kabam/rotmg/messaging/impl/GameServerConnection.as").read_text()
    # These are the public packet IDs, excluding unrelated private constants.
    packets = {name: int(value) for name, value in re.findall(r'public static const (\w+):int\s*=\s*(\d+);', connection)}
    stat_file = (src / "kabam/rotmg/messaging/impl/data/StatData.as").read_text()
    stats = {name: int(value) for name, value in re.findall(r'public static const (\w+):int\s*=\s*(\d+);', stat_file)}
    is_string = re.search(r'function isStringStat\(\).*?\n\s*}', stat_file, re.S)
    if not is_string:
        raise ValueError("String stat discriminator not found")
    string_stats = [stats[name] for name in re.findall(r'case (\w+):', is_string[0])]
    definitions = []
    for p in sorted(asset_dir.glob("EmbeddedData_*.dat")):
        # Some other .dat assets are OBJ/Pixel Bender; EmbeddedData files are XML.
        xml = ET.fromstring(p.read_bytes())
        definitions.append(dict(path=p.relative_to(root).as_posix(), tag=xml.tag,
                                objects=len(xml.findall("Object")), ground=len(xml.findall("Ground"))))
    data_source = (asset_dir / 'EmbeddedData.as').read_text()
    data_fields = dict(re.findall(r'(\w+):Class\s*=\s*(\w+)\s*;', data_source))
    registered_data = []
    for kind in ['groundFiles', 'objectFiles', 'regionFiles']:
        declaration = re.search(rf'{kind}:Array\s*=\s*\[([^\]]*)\]', data_source)
        if not declaration:
            raise ValueError(f'Missing data registration: {kind}')
        for field in re.findall(r'new (\w+)\(\)', declaration[1]):
            wrapper = asset_dir / (data_fields[field] + '.as')
            embed = re.search(r'\[Embed\(source="([^"]+)"', wrapper.read_text())
            if not embed:
                raise ValueError(f'Missing data embed: {wrapper}')
            registered_data.append(dict(kind=kind, path=(wrapper.parent / embed[1]).relative_to(root).as_posix()))
    source_files = sorted(p for p in src.rglob("*") if p.is_file())
    digest = hashlib.sha256()
    for p in source_files:
        digest.update(p.relative_to(root).as_posix().encode())
        digest.update(b'\0')
        digest.update(hashlib.sha256(p.read_bytes()).digest())
    return dict(revision=git(root, "rev-parse", "HEAD"), dirty=git(root, "status", "--porcelain") != "",
                source_sha256=digest.hexdigest(), source_root=str(root),
                actionscript_files=sum(p.suffix == ".as" for p in source_files), assets=assets,
                sheets=sheets, animated=animated, packets=packets, stats=stats,
                string_stats=string_stats, definitions=definitions, registered_data=registered_data)


def header(data):
    quote = lambda x: json.dumps(x, ensure_ascii=True)
    lines = ['// Generated by import_flash.py. Do not edit.', '#pragma once',
             '#include <array>', '#include <cstdint>', '#include <string_view>',
             'namespace realm::flash {',
             f'inline constexpr std::string_view revision = {quote(data["revision"])};',
             f'inline constexpr std::string_view source_root = {quote(data["source_root"])};',
             f'inline constexpr std::string_view source_sha256 = {quote(data["source_sha256"])};',
             'struct Asset { std::string_view path; int width, height; };',
             'struct Sheet { std::string_view name; int asset, width, height, count; };',
             'struct DataFile { std::string_view kind, path; };',
             'struct AnimatedSheet { std::string_view name; int asset, mask, width, height, strip_width, strip_height, first; };']
    for enum, items in [('PacketId', data['packets']), ('StatId', data['stats'])]:
        lines.append(f'enum class {enum} : std::uint8_t {{')
        lines.extend(f'    {name} = {value},' for name, value in items.items())
        lines.append('};')
    lines.append(f'inline constexpr std::array<Asset, {len(data["assets"])}> assets = {{{{')
    lines.extend(f'    {{{quote(a["path"])}, {a["width"]}, {a["height"]}}},' for a in data['assets'])
    lines.append('}};')
    lines.append(f'inline constexpr std::array<DataFile, {len(data["registered_data"])}> data_files = {{{{')
    lines.extend(f'    {{{quote(d["kind"])}, {quote(d["path"])}}},' for d in data['registered_data'])
    lines.append('}};')
    lines.append(f'inline constexpr std::array<Sheet, {len(data["sheets"])}> sheets = {{{{')
    lines.extend(f'    {{{quote(s["name"])}, {s["asset"]}, {s["width"]}, {s["height"]}, {s["count"]}}},' for s in data['sheets'])
    lines.append('}};')
    lines.append(f'inline constexpr std::array<AnimatedSheet, {len(data["animated"])}> animated = {{{{')
    lines.extend('    {' + ', '.join([quote(s['name'])] + [str(s[k]) for k in
        ['asset', 'mask', 'width', 'height', 'strip_width', 'strip_height']] + ['0' if s['first'] == 'RIGHT' else '2']) + '},'
        for s in data['animated'])
    lines.append('}};')
    lines.append(f'inline constexpr std::array<std::uint8_t, {len(data["string_stats"])}> string_stats = '
                 '{' + ', '.join(map(str, data['string_stats'])) + '};')
    lines.append('}')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flash-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = scan(args.flash_root.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'flash_assets.hpp').write_text(header(data))
    (args.output / 'source_inventory.json').write_text(json.dumps(data, indent=2) + '\n')
    print(f"Flash {data['revision'][:12]}: {len(data['assets'])} PNGs, "
          f"{len(data['sheets'])} image sets, {len(data['animated'])} animated sets, {len(data['packets'])} packet IDs")


if __name__ == '__main__':
    main()
