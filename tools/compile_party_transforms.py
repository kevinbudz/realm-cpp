#!/usr/bin/env python3
"""Embed the measured PartyPanel ColorTransform corrections as a native constant."""
import hashlib
import json
from pathlib import Path
import struct
import sys
source, output = map(Path, sys.argv[1:])
metadata = json.loads(source.with_suffix('.json').read_text())
data = source.read_bytes()
if len(data) != metadata['bytes'] or hashlib.sha256(data).hexdigest() != metadata['sha256']:
    raise AssertionError('PartyPanel transform corrections are corrupted')
values = struct.unpack(f'<{len(data) // 4}I', data)
if list(values) != sorted(values):
    raise AssertionError('PartyPanel transform corrections are not sorted')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text('#pragma once\n#include <array>\n#include <cstdint>\nnamespace realm {\n'
                  f'inline constexpr std::array<std::uint32_t,{len(values)}> flash_party_transform_corrections{{\n' +
                  '\n'.join(','.join(map(str, values[start:start+16])) + ',' for start in range(0, len(values), 16)) + '\n};\n}\n')
