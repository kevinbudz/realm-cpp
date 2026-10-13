#!/usr/bin/env python3
"""Check ownership using the actual configured CMake source and link graph.

Reject upward includes even when a common include directory would let them
compile. Public headers must only use PUBLIC dependencies; .cpp files may use
PRIVATE dependencies. Pure libraries may not include graphics SDKs.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADERS = json.loads((ROOT / 'cmake/header-owners.json').read_text())
GRAPHICS = {'realm_render', 'realm_ui_gpu', 'realm_platform_sdl', 'realm_startup', 'realm_ui_rmlui', 'realm_ui_views', 'realm_application'}
# The RmlUi host owns RmlUi's GL3 renderer and queries GPU time itself.
GL_OWNERS = {'realm_render', 'realm_platform_sdl', 'realm_ui_rmlui'}
SDK = re.compile(r'#\s*include\s*[<"](?:GL/|SDL|SDL2/|EGL/|GLES)')
INCLUDE = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')

def manifest(path):
    targets = {}
    for line in path.read_text().splitlines():
        owner, deps, public, sources = line.split('\t')
        # Static PRIVATE links appear as LINK_ONLY in the interface: consumers
        # link them, but their public headers cannot include them.
        targets[owner] = {
            'deps': set(deps.split(';')),
            'public': {d for d in public.split(';') if not d.startswith('$<')},
            'sources': sources.split(';'),
        }
    return targets

def closure(targets, owner, public=False):
    found = {owner}
    pending = [owner]
    while pending:
        current = pending.pop()
        if current not in targets:
            continue
        for dep in targets[current]['public' if public else 'deps'] - found:
            found.add(dep)
            pending.append(dep)
    return found

def violations(text, path, owner, permitted):
    failures = []
    if owner not in GRAPHICS and SDK.search(text):
        failures.append(f'{path}: graphics SDK in headless {owner}')
    if owner not in GL_OWNERS and re.search(r'#\s*include\s*[<"](?:GL/|EGL/|GLES)', text):
        failures.append(f'{path}: GL SDK outside the backend/platform adapter')
    for header in INCLUDE.findall(text):
        name = 'include/' + header if header.startswith('realm/') else str(Path(path).parent / header)
        other = HEADERS.get(name)
        if other and other not in permitted:
            failures.append(f'{path}: {owner} includes {name} owned by {other} without a dependency')
    return failures

def main():
    targets = manifest(Path(sys.argv[1]))
    failures = []
    owners = {}
    for owner, target in targets.items():
        for source in target['sources']:
            assert source not in owners, f'{source} compiled by multiple libraries'
            owners[source] = owner
            failures += violations((ROOT / source).read_text(), source, owner, closure(targets, owner))
    assert set(HEADERS) == {str(p.relative_to(ROOT)) for p in (ROOT/'include/realm').glob('*.hpp')} | {
        str(p.relative_to(ROOT)) for p in (ROOT/'src').glob('application_*.hpp')} | {
        str(p.relative_to(ROOT)) for p in (ROOT/'src').glob('startup_*.hpp')} | {
        str(p.relative_to(ROOT)) for p in (ROOT/'src').glob('renderer_*.hpp')}, 'Public/internal header missing an owner'
    for header, owner in HEADERS.items():
        # Graphics owners are optional in a headless build; their headers are
        # audited against the actual dependency graph in graphics builds.
        if owner in targets:
            failures += violations((ROOT/header).read_text(), header, owner, closure(targets, owner, public=header.startswith('include/')))
        elif owner not in GRAPHICS:
            assert False, f'Headless owner absent: {owner}'
    # Prove this checker rejects the regressions it is meant to prevent.
    for spelling in ['"realm/renderer.hpp"', '<realm/renderer.hpp>']:
        assert violations('#include ' + spelling, 'src/world.cpp', 'realm_game', closure(targets, 'realm_game'))
    assert violations('#include <GL/glew.h>', 'src/font.cpp', 'realm_assets', closure(targets, 'realm_assets'))
    assert violations('#include "realm/hud.hpp"', 'include/realm/model.hpp', 'realm_assets', closure(targets, 'realm_assets', True))
    assert not failures, '\n'.join(failures)
    print(f'Client boundaries: {len(targets)} libraries, {len(owners)} sources, {len(HEADERS)} public/internal headers; upward and graphics includes rejected')

if __name__ == '__main__':
    main()
