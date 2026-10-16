# Deca UI kit as vector art

The Flash client's `io.decagames.rotmg.ui` package (popups, `SliceScalingButton`, `UITabs`, `UIScrollbar`, input fields, list items, dividers) draws every element from one PNG texture atlas, stretched with 3grid/9grid slices. realm-cpp redraws that atlas as SVG in `ui/deca/`, so the art is rasterized at the drawable's density and stays sharp at any UI scale.

## Using it from RmlUi

Link the kit after the base sheets:

```html
<link type="text/rcss" href="flash.rcss"/>
<link type="text/rcss" href="text.rcss"/>
<link type="text/rcss" href="deca.rcss"/>
```

`ui/deca.rcss` maps the Flash classes to RCSS classes:

| Flash | RCSS |
| --- | --- |
| `UIPopup`, `ModalPopup`, `PopupHeader` backgrounds | `.deca-popup`, `.deca-popup-decorated`, `.deca-inset`, `.deca-inset-decorated`, `.deca-popup-header`, `.deca-popup-footer`, `.deca-popup-title`, `.deca-modal-title`, `.deca-header-button-decor` |
| `DefaultLabelFormat` titles and modal text | `.deca-title`, `.deca-title-medium`, `.deca-title-small`, `.deca-modal-title-text`, `.deca-modal-text` |
| `SliceScalingButton` (green) | `.deca-button`, with `:hover`, `:hover:active` and `[disabled]` |
| Close, info and add buttons | `.deca-close-button`, `.deca-info-button`, `.deca-add-button`, plus the small icons (`.deca-tab-info-button`, `.deca-resources-add-button`, `.deca-refresh-icon`, `.deca-spinner-up`, `.deca-spinner-down`) |
| `TabButton`, `TabContentBackground` | `.deca-tab` (`.left`, `.right`, `.borderless`, `.open`), `.deca-tab-decor`, `.deca-tab-content` (`.filled`), `.deca-tab-inset` |
| List items | `.deca-list-item` (`.inactive`, `.indicator`, `.selected`) |
| Fields and checkboxes | `.deca-field`, `.deca-bordered-field`, `input.deca-checkbox` |
| Dividers and decorations | `.deca-divider` (`.white`), `.deca-divider-title`, `.deca-divider-small-title`, `.deca-title-decoration`, `.deca-main-button-decoration` (`.dark`), `.deca-content-button-decoration` |
| `UIScrollbar` | `.deca-scroll` on the scrolling element |

Each class paints its border box with the `deca` painter:

```css
decorator: paint(deca NAME [flip-x] [flip-y] [press] [hover] [grey]);
```

`NAME` is a file in `ui/deca/` without `.svg`. The flags are the `SliceScalingButton` states: `hover` is `Tint.add(0x00FFFF, 0.1)`, `press` is the 0.9 scale about the centre, and `grey` is `GreyScale.setGreyScale`. An unknown name or flag is a document error.

An SVG stretches only inside its `data-stretch-x` / `data-stretch-y` band, as the atlas frame's slice does in `SliceScalingBitmap`. Along an axis without a band the drawing scales, so give such elements the frame's natural size (noted per class in `deca.rcss`).

## Format

`include/realm/assets/VectorArt.hpp` documents the supported SVG subset: paths, basic shapes, linear gradients, fills, strokes, opacity and transforms. Text, images, `use`, filters, masks, clip paths and radial gradients are rejected, so an asset never silently draws differently from its source. A band is split at its edges and the geometry outside it is moved, not resampled, so outlines keep their width at any size. When an element is smaller than the fixed parts, they shrink uniformly and the band collapses.

The SVGs are embedded into the binary with the other `ui/` files (`cmake/EmbedUiFiles.cmake`), keyed `deca/NAME.svg`.

## Art source and tools

Decompiled sources such as Zolmex/RotMG-Flash-Client keep only the class stubs of `io.decagames.rotmg.ui.assets`, so the art comes from the final `client.swf` (build 1599033809). The atlas is a reference for authoring and checking; the client never loads it.

1. `tools/extract_deca_ui.py CLIENT.swf DIR [--frames]` writes the atlas (`atlas.png`), its TexturePacker frames (`frames.json`) and its slice rectangles (`slices.json`).
2. `tools/trace_deca_ui.py --atlas DIR [--jog] [--note TEXT] NAME...` traces a frame into `ui/deca/NAME.svg`. It clusters the frame's colours, traces each as an even-odd path that keeps stretch-band edges in place, and straightens 45-degree and 2:1 pixel stairs into lines. By default a straightened stair is extended to meet the edges beside it; `--jog` keeps the half-pixel step instead, which loses less area on tiny shapes. Treat the result as a starting point and check it.
3. `realm_vector_render INPUT.svg OUTPUT.png [WIDTH HEIGHT [SCALE]] [--flip-x] [--flip-y] [--press] [--hover] [--grey]` renders an SVG with the client's rasterizer.
4. `tools/compare_deca_ui.py --atlas DIR --renderer build/tests/realm_vector_render [--output DIR] [NAME...]` renders each SVG at its natural size and reports the mean channel error against its atlas frame. It fails when an SVG does not render or its size or stretch band differs from the frame. With `--output` it writes side-by-side panels.

## Coverage

49 of the atlas's 94 frames are redrawn: 15 by hand (the buttons, popup frames, scroll bar and tab containers) and 34 traced. Their mean error against the atlas is about 3.3 of 255. A vector redraw is not meant to be pixel-identical: pixel stairs become straight antialiased edges.

The other 45 frames are not vectorized. They are pictorial or feature-specific: the shop, mystery box, daily quest, pet yard, challenger, season tier and game-mode art. The screens that use them (shop, daily quests, pets, seasonal challenger) are not ported, because the pinned realm-server has no endpoints for them.

## Tests

- `vector_art` (`tests/VectorArtTests.cpp`): the stretch rules, exact rasterization, flips and grey, rejection of unsupported SVG, and that every `ui/deca` SVG parses with a whole-unit size and a 1-unit band, renders at natural and stretched sizes, and that every `paint(deca NAME)` in `deca.rcss` names one.
- `ui_rml_host` (`tests/UiRmlHostTests.cpp`): a document using `deca.rcss` paints a green button, a popup and an open tab through the GPU host, and `[disabled]` turns the button grey.
