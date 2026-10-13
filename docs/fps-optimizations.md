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
