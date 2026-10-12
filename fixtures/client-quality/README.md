# Native client workloads v1

`v1/index.json` lists pinned manifests; `catalog.json` records actual legal
object types, registered model families, Priest skin IDs and catalog dyes.
Manifests never use account files or saved settings. Nexus references the
original sibling map read-only. `tools/quality_stress.py --generate --list`
regenerates definitions explicitly; ordinary runs never regenerate them.

Population tuples in the index are requested **remote** players, enemies,
model objects and Combat projectiles. The local Priest adds one player.
A projectile is a real Combat record and World entity, not an appended quad.
The driver replenishes to the requested count before each collision update.
Hits/expiration/cover remove projectiles normally; summaries report actual
remaining counts, removals, replacements and simulated entity steps. Replenishing
is synthetic server shoot traffic, not a player's firing-rate capacity claim.

The 20 ms simulation timeline applies NEWTICK every 100 ms and retains the
production 127 ms interpolation. Render time, sleep, capture and VSync do not
advance it. Seeds and source definitions are pinned. `NEXUS` preserves the map;
other scenes use a 128x128 legal Grass map with an explicit central spawn.
Dense actors occupy ±2 tiles, cluster ±0.08, spread ±45. All dense actors start
inside the ordinary 800x600 camera footprint. Static/dynamic actors and model
rotations are independent variations. `MODELS` uses real registered geometry
from legal object definitions; some such objects are enemies and collidable.
Two registered model assets have no legal XML object; the separate production
`--model-library` component covers all 42 without inventing gameplay definitions.

Below/above-cache VARIETY uses 16/512 legal dye/skin/size combinations. CHURN
replaces up to 32 of each actor kind every five ticks and resets maps every 100.
MIXED includes combat, model overlap, names/HP, particles, HUD/minimap, chat
editing/history, inventory drag/tooltip, Options and an error modal. These call
the same production controllers/rendering; actual SDL routing is checked by the
existing GPU/input suites. A separate title run and local HTTP fixtures exercise
startup animation/screen responses. PACKETS uses a local TCP peer with application
barriers, burst framing, jitter, queue measurements and reconnect.

`expected-small.json` freezes v1 outcomes for 100 ticks of `determinism.json`
with reset_every=40. Headless, rendered and paced runs must preserve these values.
Change expectations only for a deliberate reviewed semantics/fixture revision.
