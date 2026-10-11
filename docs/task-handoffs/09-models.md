# 09-models — complete

Implemented and verified 2026-10-11. Next: **10-lighting** (not started; no successor
chat was created from this session, see "Chain hand-off").

## Implementation

Objects whose XML has a registered `<Model>` now draw as real textured/solid geometry
instead of the flat sprite billboard they used to fall back to.

- `tools/compile_models.py` (CMake step, output `flash_models.hpp`) compiles all 42
  `EmbeddedAssets.models_` OBJ files with the Stage3D parser's rules (groups split on
  `g` and material change, fan triangles (i, 0, i+1), flat face normals, `Solid*`
  groups). 2200 vertices, 1152 triangles, 44 groups.
- `src/model.cpp` / `include/realm/model.hpp`: `project_model` rotates about Z by the
  XML `Rotation`, translates to the entity position, projects with the existing
  camera (zoom, camera rotation, vertex height), culls back faces with Flash's
  screen-space winding rule, and shades flat with `0.75 + 0.25 * max(0, n . L)`.
- `src/renderer.cpp`: a second small program draws projected `MeshDraw`s (Quad kind 9)
  inside the ordinary painter-ordered stream, so models interleave with characters,
  shadows and particles by the existing depth sort. Textured groups sample the object
  sprite with nearest, repeated UVs; `Solid*` groups and texture-less objects use
  the XML `Color`.
- `src/scene.cpp`: model objects use that path and draw no sprite. `SpiderWeb` hides
  until a Wall touches it (west, north, east, south), then keeps the rotation
  0/90/180/270; the latch clears with the map generation.

Source: `kabam/rotmg/stage3D/{Renderer.as,Object3D/*}`, `engine3d/*`, `objects/{GameObject,SpiderWeb}.as`,
`kabam/rotmg/assets/EmbeddedAssets*.as/.dat`. Details are in `docs/flash-specs.md` ("3D models").

## Verification

- Build/CTest: Release `build/` and sanitizer `build-sanitize/` both built without
  warnings; **33/33 tests pass in each** (`build/models-ctest-release.log`,
  `build/models-ctest-sanitize.log`). New CTest suite `models`
  (`tests/model_tests.cpp`, 49,722 checks): every `<Model>` in object XML resolves,
  cube geometry/shading/extent, sprite-less and Solid groups, zoom, object-rotation vs
  camera-rotation equivalence for all 42 models, and a 24-angle sweep for finite,
  contiguous, in-range output.
- Real Flash reference: `python3 tests/model_reference_tests.py` builds
  `tools/flash-reference/ModelsExporter.as` against the unmodified Flash source and runs
  it in AIR (real `GameObject`/`Object3D`/`ObjectFace3D`, `SpiderWeb`, `Wall`), then renders
  the same cases through native `Scene`/OpenGL (`tools/model_inputs.cpp`).
  **110 frames**: every model at camera angle 0 and 0.6 rad, 2x zoom, explicit object
  rotations, two overlapping painter-order scenes, six web/wall cases. Silhouettes agree
  within 3 px everywhere (6 px for the sub-pixel Gate plates). Output is in
  `build/model-reference/` (`contact.png`, `comparison-<case>.png`, `flash/`, `native/`).
- Real server: `python3 tests/model_live_tests.py ../realm-server/bin/Release/net8.0/RotMG.dll`
  starts an isolated server and disposable account, and a native session checks live
  objects. The server sent 4 White Fountain model objects and all 4 projected visibly
  (Release and sanitizer clients; `build/model-reference/live-*.log`). This is
  projection-level evidence from real packets, not a rendered screenshot of a live session.

## Differences and limitations (honest)

- The Flash reference is the CPU (Object3D) path. I did not capture the Stage3D program.
  Its sampler is `clamp`, which smears texels on UVs outside 0..1 (seen on the Ghost Ship Crate top;
  other models were not checked individually); native tiles instead, matching the default (`GPURender` false) renderer.
- The web (west/east wall cases) renders the same sparse sprite pixels in Flash and native; the north/south
  cases turn the web to an edge-on plane that neither renderer shows.
- Flash's GPU path updates only the CPU object's rotation for webs and draws them without
  a wall check; native follows the CPU/`findWall` behavior.
- Shading and sampling still differ slightly: up to ~6.7% of silhouette pixels (Tesla Coil)
  differ materially by shade/texel rounding, and 1 px antialias edges differ. Reference
  frames omit the native ground shadows (Flash draws them in a separate pass).
- Gates (Gate, Gate Entry, Gate Entry 2) are thin plates and vanish edge-on at Rotation 0 in both.
- Models are not depth buffered, exactly like Flash (painter order + back-face culling).
  Non-convex self-overlap follows file order.
- Each visible model allocates one small `MeshDraw` per frame. No whole-client or
  per-model performance measurement was taken; no speedup is claimed.
- Not exercised: hovering/picking model objects, and an interactive viewer session with a
  resized window (projection is recomputed every frame from the current camera).

## Chain hand-off

Per the chain procedure a successor chat for **10-lighting** would be created with
`create_thread`; that tool is not available in this session, so none was created and no
`10-lighting-dispatch.json` exists. Start 10-lighting from `docs/client-task-chain.md`.
Preserve the uncommitted work, sibling repositories, source art/fonts, particles, dyes,
skins, authoritative stats, teleport flag/cooldown, snapshots and input guards.
Lighting must respect existing model shading and painter order; status overlays stay with task 11.
