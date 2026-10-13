#!/usr/bin/env python3
"""Compile the registered Flash OBJ models into a constexpr native table.

Mirrors kabam/rotmg/stage3D/Object3D/Model3D_stage3d.readBytes: groups split on
`g` and on material changes, faces fan-triangulated as (i, 0, i+1), one flat
face normal from face[0], face[1], face[last], and vertices shared only when the
position/uv tuple and the face normal agree. UVs are stored as the texture
coordinate measured from the top of the unflipped sprite (the Stage3D path
stores 1 - vt and samples a vertically flipped copy).
"""
import json, math, re, struct, sys
from pathlib import Path

inventory, output = map(Path, sys.argv[1:3])
root = Path(json.loads(inventory.read_text())['source_root'])
asset_dir = root / 'src/kabam/rotmg/assets'
assets = (asset_dir / 'EmbeddedAssets.as').read_text()
declared = dict(re.findall(r'private static var (\w+):Class\s*=\s*(\w+)\s*;', assets))
registry = re.search(r'models_:Object\s*=\s*\{(.*?)\n\s*\};', assets, re.S)
if not registry:
    raise ValueError('EmbeddedAssets.models_ was not found')
entries = re.findall(r'"([^"]+)"\s*:\s*new (\w+)\(\)', registry[1])
if not entries:
    raise ValueError('EmbeddedAssets.models_ has no entries')


def embed_path(field):
    wrapper = asset_dir / (declared[field] + '.as')
    embed = re.search(r'\[Embed\(source="([^"]+)"', wrapper.read_text())
    if not embed:
        raise ValueError(f'Missing model embed: {wrapper}')
    return (wrapper.parent / embed[1]).resolve()


def face_normal(face, positions):
    def point(tuple_):
        i = int(tuple_.split('/')[0], 10) - 1
        if i < 0 or i * 3 + 2 >= len(positions):
            raise ValueError(f'Face vertex {tuple_} is out of range')
        return positions[i * 3:i * 3 + 3]
    p0, p1, p2 = point(face[0]), point(face[1]), point(face[-1])
    u = [p1[k] - p0[k] for k in range(3)]
    v = [p2[k] - p0[k] for k in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    length = math.sqrt(sum(c * c for c in n))
    return [c / length for c in n] if length > 0 else n


def parse(text):
    groups, positions, uvs = [], [], []
    material, group = '', None
    for line in re.split(r'[\r\n]+', text):
        line = line.strip()
        if not line or line[0] == '#':
            continue
        fields = line.split()
        key = fields[0].lower()
        if key == 'v':
            positions += [float(c) for c in fields[1:4]]
        elif key == 'vt':
            uvs += [float(fields[1]), float(fields[2])]
        elif key == 'f':
            if len(fields) < 4:
                raise ValueError(f'Face needs three vertices: {line}')
            if group is None:
                group = dict(material=material, faces=[]);groups.append(group)
            group['faces'].append(fields[1:])
        elif key == 'g':
            group = dict(material=material, faces=[]);groups.append(group)
        elif key == 'usemtl':
            material = fields[1]
            if group is None or not group['faces']:
                if group is not None:
                    group['material'] = material
            elif group['material'] != material:
                group = dict(material=material, faces=[]);groups.append(group)
    vertices, lookup, result = [], {}, []
    for group in groups:
        indices = []
        for face in group['faces']:
            normal = face_normal(face, positions)
            key_normal = tuple(normal)

            def merge(tuple_):
                key = (tuple_, key_normal)
                if key in lookup:
                    return lookup[key]
                parts = tuple_.split('/')
                p = int(parts[0], 10) - 1
                if len(parts) > 1 and parts[1]:
                    t = int(parts[1], 10) - 1
                    if t < 0 or t * 2 + 1 >= len(uvs):
                        raise ValueError(f'UV {tuple_} is out of range')
                    u, v = uvs[t * 2], uvs[t * 2 + 1]
                else:
                    u, v = 0.0, 1.0  # Stage3D stores (0,0) after its 1 - vt flip, i.e. 1 from the top.
                vertices.append((*positions[p * 3:p * 3 + 3], *normal, u, v))
                lookup[key] = len(vertices) - 1
                return lookup[key]
            for i in range(1, len(face) - 1):
                indices += [merge(face[i]), merge(face[0]), merge(face[i + 1])]
        result.append((group['material'][:5] == 'Solid', indices))
    return vertices, result


def f32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


vertex_rows, index_rows, group_rows, model_rows = [], [], [], []
for name, field in entries:
    path = embed_path(field)
    vertices, groups = parse(path.read_bytes().decode('utf-8', 'replace'))
    if not vertices or not groups:
        raise ValueError(f'Model {name} has no geometry')
    first_group = len(group_rows)
    for solid, indices in groups:
        if indices:
            group_rows.append((int(solid), len(index_rows), len(indices)))
            index_rows += indices
    model_rows.append((name, len(vertex_rows), len(vertices), first_group, len(group_rows) - first_group))
    vertex_rows += vertices

lines = ['// Generated from the registered Flash OBJ models; do not edit.', '#pragma once',
         '#include <array>', '#include <cstdint>', 'namespace realm::flash {',
         'struct ModelVertexData { float x, y, z, nx, ny, nz, u, v; };',
         'struct ModelGroupData { std::uint8_t solid; std::uint32_t first_index, index_count; };',
         'struct ModelData { const char* name; std::uint32_t first_vertex, vertex_count, first_group, group_count; };']
lines.append(f'inline constexpr std::array<ModelData,{len(model_rows)}> model_table{{{{')
lines += ['{' + json.dumps(n) + f',{a},{b},{c},{d}' + '},' for n, a, b, c, d in model_rows]
lines.append('}};')
lines.append(f'inline constexpr std::array<ModelGroupData,{len(group_rows)}> model_groups{{{{')
lines += ['{' + f'{a},{b},{c}' + '},' for a, b, c in group_rows]
lines.append('}};')
lines.append(f'inline constexpr std::array<std::uint32_t,{len(index_rows)}> model_indices{{')
for i in range(0, len(index_rows), 64):
    lines.append(','.join(map(str, index_rows[i:i + 64])) + ',')
lines.append('};')
lines.append(f'inline constexpr std::array<ModelVertexData,{len(vertex_rows)}> model_vertices{{{{')
for v in vertex_rows:
    lines.append('{' + ','.join(repr(f32(c)) + 'F' for c in v) + '},')
lines += ['}};', '}']
output.write_text('\n'.join(lines) + '\n')
print(f'Models: {len(model_rows)} models, {len(group_rows)} groups, {len(index_rows) // 3} triangles, {len(vertex_rows)} vertices')
