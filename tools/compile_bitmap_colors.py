#!/usr/bin/env python3
"""Embed the measured Flash color table as a native constant."""
import hashlib
import json
from pathlib import Path
import sys
source, output = map(Path, sys.argv[1:])
metadata = json.loads(source.with_suffix('.json').read_text())
data = source.read_bytes()
if len(data) != 65536 or hashlib.sha256(data).hexdigest() != metadata['sha256']:
    raise AssertionError('Flash color table is corrupted')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('#pragma once\n#include <array>\n#include <cstdint>\nnamespace realm {\n'
                  'inline constexpr std::array<std::uint8_t,65536> flash_bitmap_colors{\n' +
                  '\n'.join(','.join(map(str, data[start:start+256])) + ',' for start in range(0, len(data), 256)) + '\n};\n}\n')
