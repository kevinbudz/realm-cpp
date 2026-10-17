# Frame-work optimizations

The gameplay defaults use GPU minimap composition, retained terrain geometry, adaptive quad streaming, and cached derived HUD state. `realm_client` uses these implementations.

## Behavior

- The minimap remembers the map in a GPU texture at one pixel per tile. Only changed fog-memory rows upload; markers, the rotated player arrow, and zoom buttons are clipped GPU quads. Moving the view does not rasterize or upload a full minimap image. The CPU renderer remains a differential reference.
- Ground geometry is cached in 8×8 chunks and combined into one retained GPU draw for the visible patch. Camera motion changes an affine uniform; scrolling ground animation uses a time uniform. Tile changes invalidate affected chunks and their neighbouring compositors. Live texture-slot stamps prevent reuse of evicted imagery. At most 1,024 inactive/active chunks are retained under the normal camera bounds; unused chunks are evicted beyond that budget. Entity/model painter order and ground-top rendering retain their existing paths.
- OpenGL 4.4 / `ARB_buffer_storage` enables a coherent, persistently mapped three-slot stream for uploads of at least 64 KiB. Smaller streams use orphaning, which performed better on the tested Nexus workload. Busy mapped slots use separate orphaned storage without waiting. OpenGL 3.3 remains supported. Uniform locations, stable VAO attribute state, texture parameters and texture-size limits are cached; image uploads reuse shared scratch memory.
- Derived HUD inventory/interaction state refreshes immediately on world/packet revisions, local inventory actions, party revisions and interaction-target changes. A fallback refresh every 8 ms covers direct public entity/stat writes. Hover, drag, local movement, combat, and party health flashes continue to use the frame clock. Party membership was already limited to 500 ms; its frequency is unchanged.

## Measurement

See `fps-optimization-results.json` for the recorded summary. A hidden 1920×1080 Nexus pan fixture with six moving players and the RmlUi HUD ran for 600 frames, excluding 30 warmup frames, on an RX 6700 XT with Mesa/OpenGL 4.6.

The reference path averaged 0.678 ms of CPU event/build/submission work. The final optimized path averaged 0.096 ms, about an 86% reduction. The reference path's minimap rasterization averaged 427 µs. Average texture uploads fell from approximately 480 KB/frame to 1 KB/frame. These are fixture measurements with telemetry enabled, not a live-session FPS prediction; swaps, driver behavior and workload affect delivered FPS.

## Compare or diagnose

- `--telemetry PATH.jsonl` records phase timings, upload bytes, CPU/GPU/present time and stream policy.
- The reference run above was taken with `--reference-render` (CPU minimap composition, ordinary ground-quad rebuilding, uncached HUD state) and `--orphan-stream` (persistent streaming off). `realm_client` no longer accepts either switch.

To reproduce the moving-player fixture, copy `tests/fixtures/nexus-pan.json`, set `players` to `6` and `moving` to `true`, and make its relative `map` path absolute before saving the copy outside that directory. Run `realm_client --stress COPY.json --stress-summary SUMMARY.json --width 1920 --height 1080 --hidden --benchmark --frames 600 --telemetry TIMINGS.jsonl`.

Focused validation (CTest names; recorded 2026-10-12, not a full-suite run): `frame_caches`, `minimap_cache`, `terrain_cache`, `inventory_actions`, and `client_boundaries`. The render check covers panning/rotation/zoom, tile and neighbour invalidation, both ground animation modes, map transitions, minimap HiDPI and edge clipping, mapped-stream growth/reuse, and comparison against orphan streaming. Raster edge rounding allows small pixel differences at fractional scale. All five checks passed; the full suite was not run.

## Counters and GPU attribution

- Walls (kind 17) draw inside sprite runs with the same texture, so their GPU time is in the world bucket (`world_ms`). `models_ms` and `models_measured`/`models_total` cover kind-9 model runs only.
- `models` counts model instances and wall instances, one per instance. `quads` counts sprite quads and terrain instances, not walls.
- `triangles` counts two per sprite quad and each wall's corners / 3 as submitted, before the shader's back-face cull, so culled triangles still count.
- A triangle-list run (any run holding a wall) gives every instance `max(6, most corners in the run)` vertices. Shorter walls and sprites in that run carry padded vertices; the counters do not include the padding.
- A clipped layer (kind 14, the minimap today) clips its walls as well as its sprites.
- One wall store per submission: quads and overlay together may reference one `WallGeometryStore`. Scene builds a frame's walls into one store and replaces it only at the start of a build; a submission that mixes stores throws.

## 2026-10-17 pass

Every change keeps output identical: 1280×720 captures of nexus-pan, S1-mixed-dense, ui, churn, variety-512 and models-varied-rotating are byte-identical to d409cf3, and the stress summaries match.

- RmlUi's GL3 backend (`third_party/rmlui`, see `VENDORED.md`) clears, resolves and composites only the region its geometry touched, not the whole 2× MSAA layer. A frame with no UI geometry skips the pass entirely. This was the largest fill-rate cost on weak GPUs.
- All first-party libraries build with release IPO. The max-FPS limiter sleeps instead of busy-spinning. The renderer skips redundant surface binds and unchanged minimap layer uploads.
- Scene build has no per-frame sort of ground tops. Flash tints, skins, wall classes and model indices are looked up through indices. Appearance compositions survive map changes.
- Combat caches launch-angle trigonometry. Particles fold their filter rebuilds into existing passes and use an enum for emitter kinds. Chat, guild rows and dyed portraits reuse rasterized bitmaps. Compose hoists per-texel decisions out of its loops.
- Flash PNGs are decoded once at startup and shared by atlas, terrain, minimap, appearances and animations.

Median CPU ms per frame, `realm_client --benchmark --hidden` at 1920×1080, 400 frames, Mesa llvmpipe on 4 vCPUs. This is software rendering, so GPU work shows up as CPU time; it is not a hardware prediction.

| Fixture | d409cf3 | this pass |
| --- | ---: | ---: |
| nexus-pan | 36.0 | 12.0 |
| S2-mixed-dense | 255.0 | 154.8 |
| S3-combat-cluster | 672.2 | 500.1 |
| churn | 63.1 | 50.2 |
