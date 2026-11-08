#!/usr/bin/env python3
"""Check ownership using the actual configured CMake source and link graph.

Reject upward includes even when a common include directory would let them
compile. Public headers must only use PUBLIC dependencies; .cpp files may use
PRIVATE dependencies. Pure libraries may not include graphics SDKs. Transcendental
functions must come from realm::math (src/util/PortableMath.cpp), never from the C
library: libm rounds the last bit differently per platform.
"""
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADERS = json.loads((ROOT / 'cmake/header-owners.json').read_text())
GRAPHICS = {'realm_render', 'realm_ui_gpu', 'realm_platform_sdl', 'realm_startup', 'realm_ui_rmlui', 'realm_ui_views', 'realm_application'}
# The RmlUi host owns RmlUi's GL3 renderer and queries GPU time itself.
GL_OWNERS = {'realm_render', 'realm_platform_sdl', 'realm_ui_rmlui'}
SDK = re.compile(r'#\s*include\s*[<"](?:GL/|SDL|SDL2/|EGL/|GLES)')
# The Vulkan SDK belongs to the Vulkan backend (src/stage3D/vk/) and to the platform adapter that
# creates its surface (src/platform/). Only the graphics owners may include it there.
VK_OWNERS = GL_OWNERS
VK_PATHS = ('src/stage3D/vk/', 'src/platform/', 'src/lib/ui/vk')
VK_SDK = re.compile(r'#\s*include\s*[<"](?:vulkan/|vk_mem_alloc|VkBootstrap)')
INCLUDE = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
# libm transcendental functions (and float variants). Only realm::math may call them; see PortableMath.hpp.
LIBM_NAME = r'(?:sincos|sin|cos|tan|atan2|atan|hypot|exp|log2|log)f?'
# A qualified name (std::sin, ::sin), including a use as a value; realm::math::sin is not matched.
LIBM_QUALIFIED = re.compile(r'(?<![\w:.>])(?:std::|::)' + LIBM_NAME + r'\b')
# An unqualified call: with using-declarations or <math.h> it resolves to the C library. Member calls
# (x.sin(, p->sin() and realm::math::sin( are not matched.
LIBM_CALL = re.compile(r'(?<![\w:.>])' + LIBM_NAME + r'\s*\(')
LIBM_EXEMPT = {'include/realm/util/PortableMath.hpp', 'src/util/PortableMath.cpp'}
# Declarations that happen to share a libm name; not calls. Keyed by the trimmed source line.
LIBM_ALLOWED = {('src/stage3D/gl/RendererShader.hpp', "std::string log(4096, '\\0');")}
# Comments, string literals and raw string literals are blanked (newlines kept) before matching.
CODE_TOKEN = re.compile(r'R"([^(\s"]*)\((.*?)\)\1"|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', re.S)
CXX_SUFFIXES = {'.cpp', '.hpp', '.h', '.cc', '.inl'}

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
    if VK_SDK.search(text) and (owner not in VK_OWNERS or not path.startswith(VK_PATHS)):
        failures.append(f'{path}: Vulkan SDK outside the Vulkan backend/platform adapter')
    for header in INCLUDE.findall(text):
        name = 'include/' + header if header.startswith('realm/') else os.path.normpath(Path(path).parent / header)
        other = HEADERS.get(name)
        if other and other not in permitted:
            failures.append(f'{path}: {owner} includes {name} owned by {other} without a dependency')
    return failures

def code_only(text):
    return CODE_TOKEN.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text)

def libm_violations(text, path):
    failures = []
    original = text.split('\n')
    code = code_only(text)
    for pattern in (LIBM_QUALIFIED, LIBM_CALL):
        for match in pattern.finditer(code):
            line = code.count('\n', 0, match.start())
            source = original[line].strip()
            if (path, source) in LIBM_ALLOWED:
                continue
            failures.append(f'{path}:{line + 1}: libm {match.group(0).strip()} (use realm::math, see include/realm/util/PortableMath.hpp)')
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
    assert set(HEADERS) == {str(p.relative_to(ROOT)) for p in (ROOT/'include/realm').rglob('*.hpp')} | {
        str(p.relative_to(ROOT)) for p in (ROOT/'src').rglob('*.hpp')}, 'Public/internal header missing an owner'
    for header, owner in HEADERS.items():
        # Graphics owners are optional in a headless build; their headers are
        # audited against the actual dependency graph in graphics builds.
        if owner in targets:
            failures += violations((ROOT/header).read_text(), header, owner, closure(targets, owner, public=header.startswith('include/')))
        elif owner not in GRAPHICS:
            assert False, f'Headless owner absent: {owner}'
    # Prove this checker rejects the regressions it is meant to prevent.
    for spelling in ['"realm/stage3D/Renderer.hpp"', '<realm/stage3D/Renderer.hpp>']:
        assert violations('#include ' + spelling, 'src/map/World.cpp', 'realm_game', closure(targets, 'realm_game'))
    assert violations('#include <GL/glew.h>', 'src/assets/FontBook.cpp', 'realm_assets', closure(targets, 'realm_assets'))
    # Vulkan headers: allowed only in the Vulkan backend and platform adapter; rejected elsewhere.
    assert violations('#include <vulkan/vulkan.h>', 'src/map/Scene.cpp', 'realm_render', closure(targets, 'realm_render'))
    assert violations('#include <vulkan/vulkan.h>', 'src/stage3D/Atlas.cpp', 'realm_game', closure(targets, 'realm_game'))
    assert not violations('#include <vulkan/vulkan.h>', 'src/stage3D/vk/VulkanBackend.cpp', 'realm_render', closure(targets, 'realm_render'))
    assert violations('#include "realm/hud/Hud.hpp"', 'include/realm/engine3d/Model3D.hpp', 'realm_assets', closure(targets, 'realm_assets', True))
    # libm: the rule scans every C++ file under src/ and include/, except the portable module itself.
    libm_files = [p for d in ('src', 'include') for p in sorted((ROOT / d).rglob('*')) if p.suffix in CXX_SUFFIXES]
    libm_checked = 0
    for path in libm_files:
        rel = str(path.relative_to(ROOT))
        if rel not in LIBM_EXEMPT:
            failures += libm_violations(path.read_text(), rel)
            libm_checked += 1
    # Prove the libm rule rejects what it is meant to reject and accepts the portable spellings.
    assert libm_violations('double y = std::sin(x);', 'src/x.cpp')
    assert libm_violations('double y = ::exp(x);', 'src/x.cpp')
    assert libm_violations('auto f = std::log2;', 'src/x.cpp')
    assert libm_violations('double y = sin(x);', 'src/x.cpp')
    assert libm_violations('float y = std::sinf(x) + std::hypotf(a, b);', 'src/x.cpp')
    assert libm_violations('auto [s, c] = std::sincos(x);', 'src/x.cpp')
    assert not libm_violations('double y = realm::math::sin(x);', 'src/x.cpp')
    assert not libm_violations('auto [s, c] = realm::math::sincos(x);', 'src/x.cpp')
    assert not libm_violations('double y = obj.sin(x) + p->exp(1);', 'src/x.cpp')
    assert not libm_violations('auto e = expected(x); auto l = logger(x); double h = std::sqrt(x);', 'src/x.cpp')
    assert not libm_violations('// std::sin(x) and sin(y) in a comment\n/* exp( */', 'src/x.cpp')
    assert not libm_violations('const char* s = "std::log(x)"; auto r = R"glsl(sin(x))glsl";', 'src/x.cpp')
    assert not libm_violations("std::string log(4096, '\\0');", 'src/stage3D/gl/RendererShader.hpp')
    assert not failures, '\n'.join(failures)
    print(f'Client boundaries: {len(targets)} libraries, {len(owners)} sources, {len(HEADERS)} public/internal headers; upward and graphics includes rejected; libm calls outside realm::math rejected in {libm_checked} C++ files')

if __name__ == '__main__':
    main()
