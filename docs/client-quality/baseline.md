# Q00: initial native client baseline

Established 2026-10-11 in chat `01a12c71-9f72-7940-996a-ddb277d454d3`.
Q00's baseline acceptance passes. The complete client and the S3/144 Hz target
are **not qualified**. No C++ behavior, artwork, sibling repository or personal
account/settings file was changed. This document supersedes stale feature-gap
statements in the historical progress sections.

## Source and checkout

Native HEAD: `e093e05bdfe726023c6ee144166a9c95bb04907c`.
Flash HEAD: `17bdf079452a1c470b8895eae9cf108599e7addd`.
Server/map source HEAD: `bb75bc1e438b465986a8ada354b6332f69e0e3a6`.
Both sibling repositories were clean on inspection and were only read.

The initial dirty tree had modified `README.md` and `docs/client-task-chain.md`,
plus untracked `docs/client-quality-chain.md` and `docs/client-quality/`.
Q00 preserved these changes. The local snapshot in `build/quality-Q00` contains
`starting-status.txt`, `starting-head.txt`, `starting-diff.txt` and
`starting-files.json` (331 file hashes). It was taken immediately **after** the
Q00 claim, so the new ledger fingerprint already says in progress. Before
writing the completion documents, every inventoried file still matched that
snapshot, with none missing. Generated builds/assets were also retained.

[Portable evidence](evidence/Q00-results.json) retains exact build/test output,
benchmark command receipts and stdout, hardware inventory, memory readings,
individual file hashes and these aggregate hashes. Aggregate fingerprints are
SHA-256 of sorted, compact JSON mapping paths relative to the RotMG workspace
to individual file SHA-256 values. Full inventories remain under
`build/quality-Q00/*-files.json`; the native aggregate covers `src`, `include`
and the two CMake files, excluding evolving documentation.

| Input | Files | Aggregate SHA-256 |
| --- | ---: | --- |
| Native runtime/configuration | 108 | `66af45da6b2c6345f8a3193ca156c3c3a4170081b491f9e18cb6545a82af5b2c` |
| Flash src including assets | 1119 | `af4b51d481798849ba923e2f047dd7268181168d2f06618ffda9685ef9b91152` |
| Existing fonts directory including export references | 1544 | `bd37ffab2dc1f62676de7ae15fba3068671327e1b310d8aaa5e848181a93665e` |
| Existing HUD directory including raw art/supplements | 2505 | `0ba506a78f6d2fb01abbd7a31da234d552807c4bfe9057da59f433eb0a646a78` |
| Native finite lookup assets | 4 | `9f4ee05cbb31909fa98858773089f9f9c28e83ad1ef18e324f962ba920178700` |
| Generated headers/inventory | 6 | `e1d298a622baffb70341a786f1adbe59ddfc8f374aebaa81490ff3d0f10a3698` |

Nexus `../realm-server/Resources/Worlds/Nexus/0.jm` file SHA-256:
`17f01c2e4f910b3329ba7bc999f4e2b575cc628aa9ffde938f26c83542e46716`.
The importer reports 960 ActionScript files, 66 PNG assets, source hash
`b0fc7a4bebc8da4c28f4250026c62eab9b172c7380553213eca9cd02f695bcba`
(its own hashing convention). The installed fontpack has 34 variants, an
18,136,448-byte glyph index and 43,784,813-byte compressed glyph bundle. HUD
has 2380 main records, 110 skin records and seven guild-member PNGs. Font/HUD
directory totals include offline reference/export files and are not resident
memory. Original regular/bold Myriad outlines come from the sibling source.

## Hardware, build and settings

| Property | Actual baseline |
| --- | --- |
| CPU | Intel i7-12700F, 12 cores / 20 logical CPUs, max reported 4.9 GHz |
| RAM | About 15 GiB usable; about 4.4 GiB available and 4.4 GiB swap occupied at inventory time |
| GPU | AMD Radeon RX 6700 XT, 12 GiB dedicated VRAM, accelerated radeonsi/navi22/ACO |
| OS/driver | CachyOS, kernel `7.2.9-1-cachyos`, Mesa `26.2.4-arch3.1`, OpenGL core 4.6 |
| Display | Wayland desktop, SDL X11/Xwayland; primary DP-3 about 164.83 Hz, rotated DP-2 about 59.96 Hz |
| Power | `powerprofilesctl get`: performance; no clock locking or isolated host |
| Toolchain | GCC 16.2.1, CMake 4.4.4, Ninja 1.13.2, Python 3.14.7 |
| Dependencies | SDL/sdl2-compat 2.32.74, GLEW 2.3.1, FreeType 2.14.3 (pkg-config 26.6.20), libpng 1.6.59, libxml2 2.15.4, zlib-ng compatible 1.3.1, nlohmann-json 3.12.0 |
| Python checks | Pillow 12.3.0, numpy 2.5.3; fontTools 4.66.1 for offline exports |
| Release | `-O3 -DNDEBUG`; IPO enabled for realm_core/viewer/client, not all libraries; graphics ON, sanitizers OFF |
| Sanitize | Debug `-g`, address+undefined sanitizers, frame pointers; graphics OFF, IPO disabled |

No profiler was enabled. The renderer requests OpenGL 3.3 core, uses nearest
pixel-art sampling, and does not request MSAA. Benchmark mode requests swap
interval zero and bypasses the normal FPS cap and interactive movement branch.
The separate SDL probe verified successful interval zero, X11, hardware GL,
and 1920×1080 hidden logical/drawable dimensions. Neither saved user settings
nor credentials are supplied. Harness defaults enable implemented shadows,
HP bars, outlines, particles and ally effects; camera is centered, 45 degrees,
zoom 1. HUD text density follows drawable height/600.

The visible-window 1920×1080 request was resized by the window manager to
**1920×1024**, verified by a separate 120-frame capture. Its timing cannot be
treated as an identical 1080p pair. Hidden capture sizes were exactly 800×600,
1280×720, 1920×1080 and 3840×2160. Windowed visible runs do not establish
fullscreen presentation, input latency, physical HiDPI scaling or 144 Hz pacing.

## Reproduction and prerequisites

Run from `/home/kevin/Desktop/RotMG/realm-cpp`, on the existing local checkout:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
cmake --preset sanitize
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
python3 tools/quality_baseline.py --output build/quality-Q00-recheck
python3 tests/render_tests.py build/realm_viewer --output build/quality-Q00-recheck/gallery
```

The runner uses real current viewer flags, defaults to three 600-frame
repetitions, saves command/exit receipts, stdout and per-process kernel
high-water RSS, and separates screenshot runs from measured runs. Its memory
units are KiB on this Linux host. The final runner was also executed with
`--output build/quality-Q00/reproduce --repeats 1`, successfully including its
new visible-drawable capture. No GNU time package is needed.

Required inputs: sibling Flash source/fonts/XML/PNGs, the hashed Nexus map,
existing `build/fonts-client` and `build/hud-art` with skins/guild supplements,
installed native dependencies, and an SDL/OpenGL display. CTest's two GPU
Python fixtures additionally use `build/outline-art`; they hardcode the
`build/realm_viewer` path. Most pure tests work without a display. Some tests
share `build/fonts-client` and `build/hud-art` even under the sanitizer preset.
Do not remove/rebake these retained packs just to obtain a clean build.

On a new machine, offline pack reproduction requires the existing AIR SDK/JDK
and the relevant tools (`bake_fonts.py`, `bake_hud_art.py`, skin and guild art
exporters); base HUD baking alone is not the complete installed supplement set.
AIR is not a runtime dependency. Q00 used existing packs, not a fresh offline
asset rebuild. No server process was provisioned or started.

## Checks and preliminary measurements

Both preset configurations/builds passed against the existing build trees:
Release **42/42** CTest in **92.66 s**; ASan/UBSan **38/38** in **78.81 s**.
These are incremental builds, not clean-room dependency/package validation.
Fresh CTest covers game/protocol, animation/models/particles, cache/lifetime,
movement/combat/inventory, dialogs/social/skins/options and mock HTTP/TCP flows.
Release additionally covers postfx/status/options GPU and SDL keys. The four
excluded sanitizer suites are `postfx`, `status_gpu`, `options_gpu`, `sdl_keys`;
old graphics executables may still exist in build-sanitize but were not rebuilt
or used. The sanitizer menu/chat/trade tests have less rendering coverage.
No graphics code changed, so a new graphics sanitizer build was not required.

The strict gallery diagnostic exits **1**: `lofiEnvironment` matches, then
`lofiEnvironment2` has 512 differing pixels out of 480000, max channel error 1.
This is the same previously recorded Mesa quantization difference in the resize
progress entry, with native runtime unchanged. It is an optional exact-image
diagnostic, classified non-material to familiar appearance; it is **not a pass**.
The script stops there, so its remaining four galleries were not verified by
this run. No tolerance or test was disabled. No required baseline suite failed.

The imported Nexus is 142×130, spawn (76.5,72.5): 742 imported entities, zero
players/enemies and eight Model-tag objects, with no unresolved definitions.
The viewer adds one fixture Wizard, giving **743 resident objects**. There is
no TCP session, server tick stream or active combat. Offline benchmark mode
does not invoke the full session/world/combat update path. Scene, terrain,
models, appearances and HUD are exercised; particle time advances, but no
burst is supplied. Visible entity/model counts are not separately exported.

| Short Release workload | CPU mean ms | CPU p95 ms | Present mean ms | Present p95 ms | Last quads / draws | Peak process RSS KiB |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| Nexus hidden #1, 1080p | 0.0834 | 0.1031 | 0.0854 | 0.1402 | 1071 / 44 | 151092 |
| Nexus hidden #2, 1080p | 0.0808 | 0.0902 | 0.0812 | 0.1338 | 1071 / 44 | 151380 |
| Nexus hidden #3, 1080p | 0.0919 | 0.1669 | 0.0969 | 0.1308 | 1071 / 44 | 151768 |
| Extra quads hidden #1, 1080p | 0.1790 | 0.2025 | 0.0477 | 0.0810 | 10256 / 1 | 133512 |
| Extra quads hidden #2, 1080p | 0.1823 | 0.2125 | 0.0474 | 0.0838 | 10256 / 1 | 133884 |
| Extra quads hidden #3, 1080p | 0.1778 | 0.2068 | 0.0502 | 0.0786 | 10256 / 1 | 133796 |
| Nexus visible, actual 1920×1024 | 0.0862 | 0.1594 | 0.0767 | 0.1388 | 1017 / 44 | 150904 |

Each row reports 570 samples after the existing 30-frame warmup. CPU is the
combined event/build/upload/submit span. Runs finish in roughly 0.3 s including
startup; they do **not** satisfy the proposed 10 s warmup / 60 s × five protocol.
Nexus CPU p95 varies substantially relative to these tiny durations; do not use
them to claim small optimization gains. GPU_last values were 0.084–0.101 ms
for hidden Nexus and 0.203–0.216 ms for extra quads: delayed final queries, not
GPU distributions. CPU/GPU overlap; do not sum them into frame latency.

Extra quads use the raw gallery plus 10000 appended sprite quads: **no active
players, enemies, damaging projectiles, model population, hit searches or
network load**. All are submitted, but camera-visible count/overdraw is not
exported. The contract's exact default 800×600 extra-sprite command also passed
(`extra-quads-800.log`, CPU p95 0.2065 ms, Present p95 0.1968 ms).

One-frame captures are outside timing samples. They submitted 369/28,
574/33, 1063/44 and 3493/105 quads/draws at the four sizes, respectively.
PNG dimensions were checked and the native images visually inspected: readable
HUD, recognizable pixel art and original layout. This is native baseline
inspection, **not a new matched full-scene Flash comparison**. 4K capture peak
RSS was 179532 KiB; capture/readback allocations are distinct from gameplay.
Captures/logs: `build/quality-Q00/nexus-capture-*`, `visible-drawable.png`,
`runs.json`, `*.log`. Portable evidence retains the measurement summaries;
raw per-frame samples do not yet exist.

## Resources and frozen initial targets

Baseline atlas: one 2048×2048 RGBA page, **16 MiB** reported allocation.
Nexus appearance caches each finish at seven entries / 101168 bytes (CPU and
GPU), zero evictions. Extra-quad caches are empty. Process RSS peaks include
libraries/driver, startup, font indexes and transient buffers; they do not
measure GPU residency, steady-state leaks or subsystem allocation rates.

| Existing enforced resource bound | Current source |
| --- | --- |
| Appearance CPU and GPU caches | Each 256 entries / 32 MiB, current-frame ownership retained |
| Decoded font glyphs | 4096 / 8 MiB; indexes/compressed bundle and native field work are additional |
| HUD artwork | 128 / 4 MiB per main/skin pack; two packs up to 8 MiB |
| Status labels / GPU lookup | CPU 160 / 8 MiB; GPU lookup at most 256 cached surfaces, with frame owners beyond eviction |
| Live terrain | 8192 slots of 8×8 RGBA (2 MiB pixel store); offline map precomposes its needed variants (222 here) |
| Decorative effects | 8192 particles, 1024 emitters, 2048 queued spawns, 256 flashes, maximum lifetime 10 s |
| Text/event buffers | 48 floating texts, 64 pending status requests, 256 damage cues, 32 balloons, 1000 chat lines |
| Network | 1 MiB maximum packet, 4 MiB outgoing queue; bounded poll bytes; HTTP response 8 MiB |

Frozen profile **Q00-v1**: this machine/backend, physical **1920×1080**, full
implemented familiar visuals, original art, drawable-resolution text, shadows,
labels/HP bars, outlines, ally effects and optional particles enabled. Primary
qualification remains **MIXED S3** (3000 players + 3000 enemies + 3000 model
objects + 20000 active projectiles), including spread and all-visible dense
fixtures, correct protocol/combat and UI, and Q19's 30-minute churn/transition
soak. **144 Hz**: p95 frame interval ≤6.944 ms, p99 ≤13.889 ms. Record uncapped
capacity separately from paced visible play; use a verified actual 1080p
drawable for qualification. S4 overload must remain bounded and preserve
authoritative gameplay. 240 Hz (4.167 ms) and 4K capacity are stretch targets;
800×600 and 720p remain geometry/input controls. No target population was lowered.

Initial investigation budgets: **1 GiB peak client process RSS at primary S3**
and **256 MiB of client-owned GPU texture/surface allocations at 1080p**,
including atlas/UI/effects and retained current-frame owners. These are explicit
provisional engineering limits, not demonstrated S3 usage or enforced global
caps. The RSS limit gives about seven times the short Nexus process footprint;
the texture limit leaves headroom above the 16 MiB atlas, existing 32 MiB
appearance cache, UI/status surfaces and filters. Q01/Q02 must attribute actual
allocation/residency/churn before tightening or revising them. Keep existing
cache/queue/effect bounds above; never drop authoritative entities/projectiles
to fit a budget. Churn must settle without a persistent upward trend within
the measured repeat envelope. No baseline soak or GPU-residency audit exists.

On this ABI `sizeof(Entity)=296`, `FlyingProjectile=128`, `TileState=16`,
`Quad=112`. S3 has at least 9000 entity records plus 20000 projectile entity
records and 20000 FlyingProjectile records: **11,144,000 bytes (~10.63 MiB)**
of inline storage alone. This excludes vector capacity, hash nodes, strings,
stats, hit histories, textures/models, tiles, renderer buffers and optional
effects. Multi-hit histories can grow with hits; this is not a full memory
forecast or proof of bounded concentrated combat. The measurements do not
establish the primary target: frame intervals/p99 and production MIXED are
unavailable. Its status is **unqualified**, with component costs encouraging
but no 144 Hz/S3 pass or whole-client speedup claim.

## Feature audit and next evidence

Source/CMake inspection confirms the old ledger's completed 01–12 paths exist:
dialogs, guild board/member HTTP and permissions, minimap groups/teleport,
party arrows, skins/purchases/RESKIN, dyes/masks, particles, 42 compiled models,
post effects, status names/icons/text/breath and options/settings. Current CTest
exercises these controllers and selected protocol/GPU paths; Q00 did not
repeat every historical AIR fixture or real-server claim. Empty automated
trade evidence still does not establish a completed nonempty trade.

Confirmed outstanding gaps:

- **Q14:** Session's DEATH branch only marks dead; main shows "Your character
  has died." and returns through startup. No packet-data death/fame screen.
- **Q15:** startup Legends action explicitly opens an unavailable message.
- **Q16:** no character-delete action, endpoint adapter or confirmation in
  startup/account; absence verified across native sources.
- **Q17:** no native audio playback/backend/event integration. Sound toggles
  are stored; no sound/music playback was heard or tested.
- Startup still lacks slot purchase (explicit message), news icons/link
  actions, character-list scrollbar and some star/credit artwork. Portraits
  use static art; map editor is unavailable and remains outside this client
  quality chain's scope. Audit the other startup gaps in Q11/Q19.
- Quest portraits, guild-invite popup and UI quality selection lack native
  counterparts; cursor/fullscreen visual verification remains historical
  missing evidence. Do not mistake stored settings for working features.
- Production replay/stress, real per-axis populations, raw frame intervals,
  median/p99/max, subsystem CPU/GPU zones, allocation/upload/cache/queue
  counters and input-to-submission latency are missing. Q01 owns telemetry;
  Q02 owns representative deterministic workloads and long runs.
- Live-server compatibility, complete Flash/native scene pairs, physical
  HiDPI/input-to-display, long-term memory/churn/audio, cold-load/map hitches,
  packaging and non-Linux runtime qualification were not reverified here.

Q01 should preserve the inexpensive existing summary interface while adding
attributed asynchronous/raw telemetry and optional profiling. Keep captures
out of performance samples, expose actual drawable/backend/settings, and
measure profiler overhead. Q02 must replace component-only extrapolation with
production game/session/combat paths before any capacity claim.
