# 08-particles — complete (accepted by the user)

Implemented 2026-10-11. On 2026-10-11 the user stated that particles are
completed, so the ledger marks this task complete. The final Release/sanitizer
and real-server evidence described under Verification was not re-collected in
that session; check `build/particle-reference` and `build/particles-*.log`
if it is needed. Next: **09-models**.

## Implementation

Inventoried all 16 SHOWEFFECT types plus unknown0, AOE, nonfatal directional hit
blood and explosion feedback, friendly projectile cover debris, magenta trails,
level-up swirls, Healing-stat particles and registered object effects. See
`docs/flash-specs.md` for the source-specific event table and parameters.

`Particles` owns native presentation state separately from authoritative World
entities. Session routes SHOWEFFECT; Combat routes AOE/trails/cover impacts and
retains all existing hit, move and shot ACK messages. World routes nonfatal
blood, including source zero-damage blood without new text cues, level changes,
texture/alternate emitters and Healing condition. No HP/MP/condition mutation,
combat RNG consumption, fake combat entities or packets are introduced.

Original pixel-weighted palettes are generated at build time from every original
image set and right-standing animation texture. Transparent frames emit no white
fallback blood. XML BloodProb/BloodColor is retained. All four actual registered
texture emitters work (Fountain, Vent, Hexxer, DeHexxer). SwirlingMist's standalone
particle data/animation is imported; Gas/Bubbles/XML emitter support is present.
Other dungeon XML files declaring mist are not registered in this Flash build.
The existing model geometry gap on Fountain remains task09's ordered scope.

The fork's optional Throw duration and missing-target start rules are preserved.
Nova/Burst/Ring/Collapse/ConeBlast have source radial/path geometry; Line/Lightning
use source counts/lifetimes; Heal follows authoritative targets; Flow accelerates
toward moving targets; ThrowProjectile uses source object art and fixed-square
bitmap rotation. EntityFlash uses source sine RGB bias without raster cloning;
Jitter is presentation-only and uses the source ten-second amplitude buildup.

Scene merges particles with normal entities by ground-space depth after shadows,
before top terrain and UI. Solid particles use a shader with source integer core
sizes and thin outline; no texture is created for individual colors/sizes. Mist
and thrown art reuse the bounded appearance caches. World generation and explicit
Session disconnect clear particles/emitters/flashes/jitter. Target removal clears
following particles and custom generator-owned particles; independent source
Fountain/other debris lives out its short lifetime.

Relevant native files: `include/realm/{particles,catalog,world,combat,scene,renderer}.hpp`,
`src/{particles,catalog,world,session,combat,scene,renderer,main}.cpp`;
`tools/compile_particle_palettes.py`, `tools/particle_inputs.cpp`,
`tools/flash-reference/ParticlesExporter.as`, `tests/particle*`, CMakeLists.txt.
Source: `kabam/rotmg/messaging/impl/{GameServerConnection.as,incoming/ShowEffect.as}`;
`com/company/assembleegameclient/objects/{particles/*,thrown/*,Projectile.as,
GameObject.as,Player.as,TextureData.as,FlashDescription.as}`;
`util/{BloodComposition.as,TextureRedrawer.as}`, `map/{Map.as,Camera.as}`;
`kabam/rotmg/assets/{EmbeddedAssets_particlesEmbed.dat,EmbeddedData.as}`.
Server: `Game/Entities/Player.UseItem.cs` and `Game/Logic/Behaviors/Flash.cs`.

## Bounds and comparison limits

- 8192 live particles; 1024 emitters; 2048 accepted new particles between updates;
  256 entity flashes. Solid particles need no additional GPU textures.
- Particle lifetime at most 10000ms, including an explicit Flow cap; size core at
  most32 pixels. Radius/count caps prevent hostile effect geometry/work spikes.
  Flash is bounded to one hour, retaining the server's actual 5000-repeat/.6s
  behavior. Jitter is a fixed two-value camera state persisting to map teardown.
- Trails and sparker emissions use ~60Hz with at most three catch-up births;
  attached emitter loops are similarly bounded. Hitches expire stale particles
  and drop backlogs. Independent visual RNG leaves server-seeded combat intact.
- CPU/GPU appearances retain task07's separate 256-entry/32MiB caches and current
  frame ownership; map generations clear those caches too. Original finite
  palettes comprise 8746 textures / 303509 weighted pixels (~1.2MiB RGB words).
- Normal/resized source/native patterns match at a glance. Thin outline alpha,
  nearest boundary placement and textured halo/gradient rounding differ slightly.
  Random native trajectories/density differ from Flash and its frame-rate-dependent
  emission. No exact-pixel or whole-client speedup claim is made.
- Reference screenshots use actual Flash classes and shared source random
  geometry, rendered by the actual native Scene/OpenGL path. This validates
  rendering; independent native tests/TCP/server fixtures validate simulation.
  AIR is offline only. Existing art/fonts/HUD packs and backups remain intact.

## Verification

Final evidence is being collected under `build/particle-reference` and
`build/particles-*.log`. This status will become complete only after final
Release/sanitizer and real-server evidence is recorded.

## Successor facts

Implement **09-models** in this same checkout: actual 3D model import/rendering,
textured geometry, placement, depth/occlusion and camera/map transitions against
source scenes. Preserve all uncommitted/untracked work, sibling repositories,
source art/fonts, native particles/dyes/skins/authoritative stats, teleport map
flag and ten-second cooldown, snapshots and input guards. Background/environment
lighting and source post effects remain task10; full status overlays remain11.

After task08 acceptance, update the ledger/progress and create exactly one
09-models chat using list_projects and project 4e459a47-d36d-4862-9ff2-53c2d40e669c,
local environment, default model. Check 09-models-dispatch.json first, save the
receipt, take one zero-time wait snapshot and finish this chat. No automation,
commits, resets or subagents. Task17 has no successor.
