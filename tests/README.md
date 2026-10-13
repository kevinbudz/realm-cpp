# Tests

Tests run through CTest in the `build` tree. Configure and build first:

```sh
cmake -S . -B build
cmake --build build -j 8
```

## Labels

Every test has exactly one label, which states what the test needs:

| Label | Meaning | Prerequisites | Command |
|---|---|---|---|
| `unit` | Headless. No GL context, no display. Network only on 127.0.0.1. | Python 3. `flash_import` also needs the Flash source tree at `REALM_FLASH_ROOT` (default `../realm-client`). | `ctest --test-dir build -L unit -j 6` |
| `gpu` | Opens an SDL window or GL context, or runs `realm_client`. | A display (`DISPLAY` or `WAYLAND_DISPLAY` set) with an OpenGL 3.3 driver. Python 3 with Pillow for the status, options, legends, death-fame, delete-character and UI-bundle scripts. | `ctest --test-dir build -L gpu -j 6` |
| `live` | Needs a real game server. | Not used: no CTest test needs a live server. | none |

GPU tests share the `realm_gpu` resource lock, so they never overlap, even with `-j`.
The `sanitize` preset configures with `REALM_BUILD_VIEWER=OFF`, so it has no `gpu` tests and no `realm_client`.

To list a label's tests: `ctest --test-dir build -N -L unit`.

## Running one test

```sh
ctest --test-dir build -R '^options$' --output-on-failure
ctest --test-dir build -V -R '^options$'    # also prints the command line
```

## Adding a test

Declare tests in `CMakeLists.txt` with `realm_add_test` (C++ executable) or `realm_add_python_test` (script).
Both are defined in `cmake/RealmTests.cmake` and require `LABELS`. Each new `gpu` test that opens a window, a GL context
or a `*_gpu` script must also be added to `REALM_GPU_TESTS` in `CMakeLists.txt`, which sets its `realm_gpu` lock.

## Fixtures and inputs

Test data is in `tests/fixtures/`, described by `tests/fixtures/README.md`. It includes the legends and death-fame XML
used by unit tests, the stress manifests, and `index.json`, which lists the pinned workloads.

The `*_session_inputs.cpp`, `skin_http_inputs.cpp` and `delete_stale_inputs.cpp` files are small drivers. CMake builds
them as executables, and the Python loopback tests run them.

`tests/client_boundaries_tests.py` reads `build/client-boundaries.tsv`, which configure writes. Rerun configure if it is missing.

## Stress checksum oracle

Stress runs are deterministic. From the repository root, with `REALM_QUAD_CHECKSUM=1`:

```sh
REALM_QUAD_CHECKSUM=1 build/realm_client --hidden --stress tests/fixtures/S1-mixed-dense.json \
    --stress-summary OUT.json --frames 120
```

The summary's `quad_stream_checksum` identifies the rendered quad stream. It is accompanied by
`submitted_quads_total`, `submitted_triangles_total`, `state_checksum` and `packet_checksum`. Two runs of one manifest
must match. To check a change, compare the summaries from before and after it. The run needs a display, as for `gpu` tests.

## GPU scripts outside CTest

These scripts are not registered with CTest. Run them by hand from the repository root.

- `tests/startup_parity_gpu_tests.py` compares account and character-selection screens against disposable local HTTP
  peers on 127.0.0.1. Needs `build/realm_client`, Pillow and a display.
- `tests/ui_resize_gpu_tests.py` resizes a real SDL window and exercises retained UI uploads and menu input. Needs the
  built binaries (`--binary-dir`, default `build`), Pillow, a display, and the read-only map
  `../realm-server/Resources/Worlds/Nexus/0.jm` next to this repository.
- `tests/skin_gpu_tests.py` covers Startup purchase and CREATE, the SDL vendor chooser, RESKIN, resize and portraits.
  Needs the built binaries (`--binary-dir`, default `build`), Pillow and a display. Output goes to `build/skin-reference/gpu`.

The other `*_gpu_tests.py` and `*_hud_tests.py` scripts follow the same pattern: `realm_client --hidden` with local HTTP
fixtures. The `*_live_tests.py` scripts and `live_server_tests.py` need a real game server build and are run by hand.
