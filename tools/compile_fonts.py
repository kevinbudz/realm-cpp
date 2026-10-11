#!/usr/bin/env python3
"""Replace the exporter JSON glyph table with a compact native binary index."""
import argparse
import json
from pathlib import Path
import struct

RECORD = struct.Struct('<IBBhhHHdII2x')

def compile_pack(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('bundle_format') != 'alpha-zlib':
        raise ValueError('A compressed alpha font bundle is required')
    output = {key: manifest[key] for key in ('source_sha256', 'bundle', 'bundle_format')}
    output.update(schema=2, index='glyph-index.bin', record_bytes=RECORD.size, fonts=[])
    count = 0
    with (directory / output['index']).open('wb') as stream:
        for face in manifest['fonts']:
            if face['shadow']:
                continue
            descriptor = {key: face[key] for key in ('size', 'bold', 'shadow', 'phases', 'line_height', 'line_advance', 'kerning')}
            descriptor['vertical_phase'] = face.get('vertical_phase', 0)
            descriptor.update(first=count, count=len(face['glyphs']))
            for glyph in face['glyphs']:
                stream.write(RECORD.pack(glyph['code'], glyph['phase'], glyph['empty'], glyph['x'], glyph['y'], glyph['width'], glyph['height'], glyph['advance'], glyph['offset'], glyph['bytes']))
                count += 1
            output['fonts'].append(descriptor)
    (directory / 'fontpack.json').write_text(json.dumps(output, separators=(',', ':')))
    print(f'{count} glyph phases indexed in {count * RECORD.size:,} bytes')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    compile_pack(parser.parse_args().directory)
