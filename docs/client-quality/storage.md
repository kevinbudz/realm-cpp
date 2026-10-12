# Hot data and resource lifetimes (Q05)

Measured on the Q00-v1 machine, Release. Evidence: [Q05-results.json](evidence/Q05-results.json), handoff [Q05.md](handoffs/Q05.md).

## How to measure

```sh
./build/realm_world_storage_tests              # lifetime/stale-reference/teardown checks + container differentials (CTest `world_storage`)
./build/realm_world_storage_tests --profile    # counted heap: bytes per object/projectile, live/peak, allocations per tick, populate/teardown
python3 tools/quality_pair.py --before BINARY --after build/realm_stress --scenarios S3-mixed-dense,churn --ticks 60 --repeats 5 --output build/pair
```

The probe replaces global `operator new/delete` in that one executable (live bytes use `malloc_usable_size`), so numbers include allocator
rounding. `tools/quality_pair.py` alternates two `realm_stress` binaries over the same manifest, requires identical state/packet/hit/spawn
checksums, and reports median zone times. Both are component evidence (World + Combat + Session replay, no Scene/GPU).

## What the profile found

1. **`Particles::remove_target` was 79 % of all instructions at S3 mixed-dense.** `World::remove` called it for every expiring projectile; it
   scanned three arrays (up to 8,192 particles of 224 bytes = 1.8 MB, 1,024 emitters, 256 flashes) even though almost no object has a following
   effect. `Particles::attach` scanned the emitter list for every `World::add`. Fixed with `TargetFilter` (below).
2. `World::report_damage`/`notify` were 29 % of the next profile: both bounded logs dropped their oldest entry with `vector::erase(begin())`
   (256 `DamageCue`, 64 `StatusEvent` moves per hit once full).
3. Object stats were an `unordered_map` per entity: a player carries about 40 stats, i.e. ~40 heap nodes plus a bucket array (~1.7 KB per player: 40 x 32-byte malloc chunks plus the bucket array).
4. The id -> slot table was an `unordered_map<int,size_t>`: one heap node per entity and per projectile, freed and reallocated on every spawn/expiry.
5. `World::add` re-parsed `Tex1`/`Tex2` from XML for every new entity (every projectile).
6. Capacity retained after a crowd left: the dense list, the id table and `Combat` working vectors never shrank (3.2 MB after a 9,000-object map).

## Changes and layout

| Structure | Before | After | Notes |
| --- | --- | --- | --- |
| id -> dense slot (`World::entity_lookup_`) | `unordered_map<int32,size_t>`, heap node per entity | `EntityIndex` (`entity_index.hpp`): open addressing, 8-byte slots, linear probing, Fibonacci hash on high bits, backward-shift deletion (no tombstones), load <= 0.5, shrinks below 1/16 | zero allocations per spawn/expiry between growth steps; released at map reset |
| `Entity::stats` | `unordered_map<uint8,int32>` (56 B + node/stat) | `StatMap` (`stat_map.hpp`): sorted contiguous 8-byte entries, same lookup interface | `Entity` 304 -> 272 B; stats ~1.7 KB -> 0.32 KB (exact capacity, as `World::status` reserves from the first full status) to 0.5 KB (grown by doubling); iterators/references are invalidated by insert/erase (no caller held any) |
| `FlyingProjectile::hit` | `unordered_set<int32>` (56 B, node per hit) | `HitSet`: sorted vector (24 B empty) | `FlyingProjectile` 128 -> 96 B |
| Particle/emitter/flash target lookup | three full scans per removal / add | `TargetFilter`: 4096-bit conservative id filters, one per container, rebuilt from live contents each update | a clear bit proves no effect follows the id; a set bit only means "scan"; id -1 (untargeted) always takes the exact path |
| `damage_cues_` (cap 256) | `vector`, `erase(begin)` | `deque`, `pop_front` | consumer API unchanged for `back/size/empty` |
| `status_events_` (cap 64) | `vector`, `erase(begin)` | `vector` + head offset, compacted when the head reaches 64 | `take_status_events()` still returns a plain vector |
| Default dyes | `xml.number("Tex1"/"Tex2")` per `add` | decoded once in `ObjectDef::tex1/tex2` | |
| Capacity after a crowd | retained | `World::initialize` frees the dense list above 2,048 entries and the id table above 1,024 slots; `Combat::reset` and `update` release projectile/grid vectors that exceed 8x their size (floors 1,024 / 4,096) | limits stay explicit; no pool or large fixed reserve was added |

`Particles` keeps its existing fixed bounds (8,192 particles, 1,024 emitters, 256 flashes); those reserves are constant and were not changed.

Per-object cost (probe, counted heap, players with 40 stats / enemies with 5, probe inserts stats one by one so they grow by doubling; includes the dense list's power-of-two capacity slack, about half of the total): actors 1,006 -> 592 B (S2) and 1,139 -> 722 B (S3); projectile
(entity + `FlyingProjectile` + table share) 620 -> 511 B (S2) and 496 -> 393 B (S3). `Entity` is 272 B; it was **not** split into hot/cold
halves: `World::update` and the camera/visibility scans cost 0.05-0.3 ms per frame at S3 (Q04), `name`/`guild`/`stats` have ~350 call sites, and
a measured hot/cold gain would be well under 1 ms of 140-800 ms frames. That separation stays an option for a later task if a profile shows it.

## Stable handles and reset rules

- Entities are addressed by object id everywhere that outlives a call: `FlyingProjectile::entity_id`, particle/emitter/flash targets,
  `World::interactive_`, tile `object_id`. `World::entity()` returns a pointer valid only until the next `add`/`remove`/`initialize`
  (the dense list reallocates and swap-removes); the test adds 4,000 entities between a projectile's spawn and update to prove ids still resolve.
- `World::add` on an existing id removes the old object first (and its effects); `remove` swaps the last entity into the hole and re-points its
  table slot; `initialize` clears entities, table, interactive list, effects, logs, advances `generation()`; `Combat::reset` removes its visuals
  and releases grid storage. A removed object leaves no particle, emitter or flash behind (`Particles::targets` is the exact check the test uses).

## Limits and bounds

Growth is proportional to live objects with explicit release points (above). Nothing here adds an unbounded pool. Appearance/text/icon
cache residency and per-frame label/surface allocation (S2 mixed-dense: 52k allocations and 300 MB of image bytes per frame, almost all under
`appearance.compose`) belong to Q07, not this task.

## Cache budgets (S3 follow-up)

Appearance CPU/GPU caches 4096 entries / 64 MiB each, status labels 4096 / 64 MiB (CPU) and 4096 / 64 MiB (GPU surfaces), against the Q00 provisional 256 MiB GPU / 1 GiB RSS limits. They are LRU with in-flight draw ownership, so eviction cannot invalidate a drawn quad. A working set above these bounds still thrashes cyclically; residency policy beyond sizing is Q07.

## Per-frame entity sets and the static grid (Q05b)

A real Realm map (`realm-server/Resources/Worlds/Realm/1.wmap`) loads ~246,000 static objects into the dense list (272 B each, ~67 MB per
full pass). Six per-frame passes visited all of them; they now visit only their candidates ([handoff](handoffs/Q05b.md)).

| Structure (`include/realm/entity_sets.hpp`, kept by `World`) | Contents | Maintained in | Consumers |
| --- | --- | --- | --- |
| `DenseSets<6>`: per slot one membership byte plus one bit per set, interleaved per 64 slots (48-byte block), moved with the swap-removal | moving (superset of nonzero velocity), enemies, markers (players, Character, Portal, GuildHallPortal), players, merchants, dynamic (not static) | `add` (mask cached per type; type is fixed), `remove` (`swap_remove` touches only the sets either slot belongs to), `initialize`; moving also in `status` (interpolation sets it), `goto_entity` (clears), `set_player_id` (old focus re-checked), `update` (cleared lazily) | `World::update`, `Combat::build_target_index`, `Minimap::update`/`hover_players`, `Party::update`, `world_dyes`, `Scene` |
| `StaticGrid`: 8x8-tile cells of dense indices, cell/slot arrays parallel to the dense list | entities of a `Static` definition not added as a projectile (`World::add(..., projectile)`; Combat passes `true`) | `add`, `remove`, `initialize` (sized to the map), and every position change World makes (`goto_entity`, `update`) | `Scene::build` visibility via `Scene::static_footprint` |

Set bits are visited in ascending dense index, i.e. in the order a scan of `entities()` meets them, so first-found/tie rules and the minimap's
blend order are unchanged and callers keep their original filters. Out-of-map positions clamp to border cells (as do query bounds). Costs: ~1.75 B per
entity for the sets (~430 KB at 246k), 8 B per entity for cell/slot, 4 B per static entry plus one small vector per non-empty cell (65,536 cells on a
2048x2048 map), a 64 KB per-type mask table: about 6 MB more peak RSS on the Realm manifests (394 -> 400 MB, 423 -> 430 MB). All O(1) per
add/remove/move; nothing is rebuilt per frame. The bookkeeping costs ~7 ns per projectile spawn+removal (callgrind: +47 instructions per `add`, +76 per
`remove`), visible only under extreme churn: `combat.update` +0.12-0.15 ms per tick at 20,000 shots dying per tick (S3 mixed-dense, headless).

Invariants (documented in `world.hpp`, enforced by the differentials): `Entity::type` never changes after `add`; static entities change position only
through `World` and keep their definition's `z`; any velocity World does not set is the focus player's (movement.cpp). Code writing `position`/`z` of a
static entity or `velocity` of a non-focus entity directly would bypass the grid/moving set; nothing in `src/` or `tools/` does.
Oracles: `Scene::set_visibility_scan(true)` (culled full scan), `Scene::set_culling(false)`, `REALM_TARGET_SEARCH=scan`, and the verbatim original passes in
`tests/world_query_tests.cpp`.
