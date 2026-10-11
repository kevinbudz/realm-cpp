# Implementation progress

The project goal is a complete, optimized native C++ client using Flash as its
visual and functional shell. On 2026-10-10 the user clarified that UI/playfield
should look basically the same, with performance and improvements over Flash
taking priority over exact matching. See `port-spec.md` and `../AGENTS.md`.

Exact-match results below are historical implementation evidence. Small text
coverage, subpixel, antialiasing, shadow, and rotated-arrow differences are
accepted and do not block completion. Fix actual readability/layout defects,
missing features, correctness problems, and measured performance bottlenecks.

## Current evidence

- Native guild registrar, guild hall portal, and name changer panels/dialogs are
  implemented (2026-10-10; see the section below). All 15 CTest suites passed
  in Release and ASan/UBSan builds; affected interaction/inventory suites and
  the expanded native rendering checks passed after the final title layout fix.
- Native item tooltips are implemented (2026-10-10; see the section below).
- Before the social panels, all 13 CTest suites were validated in Release and ASan/UBSan builds.
  The sanitizer run exposed a pre-existing stale player reference in the
  merchant section of `inventory_tests.cpp`; it now reacquires the player by
  object ID after entity additions/removals. The affected inventory suite
  passed in both builds after the fix; all other suites passed the full runs.
  The optional 963-item rendering/cache/scroll checks also passed ASan/UBSan.
- Flash baseline: `17bdf079452a1c470b8895eae9cf108599e7addd`.
- Server baseline: `bb75bc1e438b465986a8ada354b6332f69e0e3a6`.
- Eleven Release and ASan/UBSan CTest suites passed after stats tab integration.
  Standalone font and HUD artwork cache stress runs also passed sanitizers.
- Core tests: 179 checks. Animation tests: 20,678 checks. Movement: 29 checks.
- Terrain test: 34,778 checks and 16,031 LRU evictions; no current-frame
  texture was overwritten and the 8,192-slot store stayed bounded.
- Independent loopback server verifies HELLO/LOAD, fragmented/coalesced input,
  world updates, PONG, GOTOACK and MOVE. It now connects via localhost to
  exercise IPv6-to-IPv4 address fallback where available.
- Isolated current real server accepted account registration, CREATE and LOAD,
  100 MOVE, 16 PLAYERSHOOT, 4 USEITEM, 3 SHOOTACK, 1 INVSWAP and 1 INVDROP
  packets. Swap, consumable use and drop each received a successful INVRESULT.
  All three sessions remained connected. Its database
  and settings were temporary. The supplied repositories remain clean.
  A fourth session now spends MP and uses a magic potion (slot 255); the
  server accepts it, refills MP and reports the stack going from 6 to 5.
  Four consecutive runs passed, one with the ASan/UBSan client. One earlier
  run timed out the 2-second inventory exercise; it now has 4 seconds.
  The inventory session now also drops the starter weapon through the
  ground-drop routing, waits for the server's soulbound bag, picks the weapon
  up with the ContainerGrid double-click path and re-equips it (3 INVSWAP,
  1 INVDROP; 6 seconds). Five consecutive runs passed. One earlier run showed
  that a stat update queued before an accepted swap can arrive ahead of its
  INVRESULT and briefly show the old slot. Flash applies server stats the same
  way, so the exercise now waits for the authoritative slot.
  A fifth session walks to the Nexus Vault Portal until it becomes the
  interaction target, sends USEPORTAL and follows RECONNECT into the Vault.
  The wire proxy now relays one connection per map. Five consecutive
  Release runs and one ASan/UBSan run passed with 6 HELLO, 5 LOAD and
  1 USEPORTAL. Three earlier back-to-back runs stopped because the test
  server exited during startup, before any client connected. Their logs were
  truncated, and it did not recur in the next five runs; the cause is unknown.
- A live Nexus frame was captured under `build/test-server/live.png`.
- 222 Nexus terrain compositions match original TileRedrawer PNGs exactly
  with the mask choice fixed to variant zero.
- `tests/animated_terrain_tests.py` now regenerates its map and source references.
  All 429 fixtures across 26 animated GroundCXML definitions match the Flash
  compositor exactly: 225 base textures, 200 transparent overlays and four
  compositions. This includes definitions with only TopAnimate.
- 48 outlined/unoutlined projectile frames matched original Point3D geometry
  on real Stage3D, across six texture/size combinations and four angles.
- A colored fixture covering every alpha byte matched Stage3D exactly after
  correcting PNG decode, premultiplied upload and SOURCE_ALPHA blending.
- All 35 filtered Nexus sprite captures and the 48 projectile captures matched
  exactly after the alpha correction.
- Combat has 31 checks, including item stat/rate/damage modifiers, ability
  MP/cooldown validation, USEITEM-before-PLAYERSHOOT ordering and authoritative HP.
- Inventory has 111 prediction, slot compatibility, packet, rollback and input
  checks, including potion stacks, container panel input/drop routing and
  the portal button, click and PortalPanel state mapping. A separate independent TCP peer verifies INVSWAP, consumable USEITEM,
  ability USEITEM/PLAYERSHOOT and INVDROP through Session, including fragmented
  rejection replies and the shared FIFO acknowledgment order.

## Fonts: implementation evidence and optional exact comparisons

- Downloads contains regular and bold Myriad files identical to the embedded
  files. SHA-256 regular: `87224cb7af1a3150172ae8190185fad761a50bd21648f8befd3a7751fc33167d`;
  bold: `0c2923e4148b20a95c813c0331f62c07c1c24e246549f78127e946b943e4fee1`.
- FontExporter uses the original SimpleText and exports glyph coverage on the
  twentieth-pixel horizontal grid, advances, line metrics and full-field samples.
- Native FontBook lays out UTF-8 and rasterizes whole fields, then applies the
  measured Flash shadow filter. The filter has horizontal and vertical
  1,2,2,2,1 taps, truncation after each pass, then alpha composition over 256.
- Coverage bundles contain compressed alpha, not hundreds of thousands of
  separate PNGs. A 32-byte binary index replaces the large exporter JSON at
  runtime. Font styles load on demand, with an O(1) LRU capped at 4,096 glyphs
  and 8 MiB decoded data. GPU storage contains complete text fields rather
  than every glyph phase.
- `build/fonts-client` has regular and bold at 12/13/14/16/18/20/22/24/26/28/30/32/36,
  including full original font Unicode coverage. Size 13 also has half-pixel
  vertical coverage for the stats tab. Size 18 bold also has vertical phases
  .2/.4/.6/.8 for wrapped PortalPanel names (`--wrapped-bold-sizes`). It
  indexes 535,744 coverage records in 17,143,808 bytes, with a 41 MiB
  compressed bundle. The previous pack is `build/fonts-client-before-portal`.
  In `tests/font_cpu_tests.py`, the 560 fields from the previous faces fail
  on exactly the same 128 fields and bounds as before. The 80 new
  vertical-phase fields have 16 failures, all in the existing categories:
  first-letter overhang and the left-aligned two-line sample.
  `build/fonts-hud` contains the earlier five-size comparison pack. Shadowed styles in the task file provide
  references; the native filter generates those from foreground glyphs.
- All 24 basic size-14 fields match both black GPU captures and complete
  transparent RGBA references exactly with the current binary loader. A stress
  run made 4,485 evictions, ended at 4,096 glyphs / 2,488,084 decoded bytes,
  and preserved those reference outputs. It also passed ASan/UBSan.
  The new twelve-size `fonts-client` pack passed the same 24 source fields
  under ASan/UBSan while evicting 16,602 glyphs; its final decoded cache was
  4,096 entries / 3,837,808 bytes.
- The current size-13/half-pixel pack also passed those 48 selected fields and
  its complete-glyph cache stress under ASan/UBSan: 20,054 evictions and a final
  cache of 4,096 glyphs / 3,513,888 decoded bytes.
- Expanded comparison: 163 of 200 full transparent fields match exactly.
  Remaining 37 involve multiline vertical subpixels, first-letter overhang
  clipping, a few individual coverage samples and one filtered size-16 field.
  Do not claim complete font parity. Font rasterization at four times the scale
  does not reproduce the original coverage by simple threshold/downsampling.
- Fonts now render the gameplay character name and XP/fame, HP and MP meters.
  Whole-field color, half-pixel placement and boost opacity are implemented.
  Functional wrapping, rich text and missing-glyph handling still need review;
  reproducing Flash's complete glyph coverage is optional.
- Size-13 half-pixel vertical coverage fixes the stats values' source placement.
  All 48 selected transparent fields (24 size-14 basic fields and 24 size-13
  half-pixel fields) match exactly. The fixed stats labels use finite original
  HUD artwork to preserve TextField clipping; this does not establish arbitrary
  clipped-field font parity. Tiny coverage differences are accepted; review
  multiline output for actual readability or layout problems.

## Gameplay HUD and minimap

### Native guild and name interactions (2026-10-10)

- GuildHallPortal shows Guild Hall with Enter for guild members and Not In
  Guild otherwise. Click and the default interact key 0 send USEPORTAL only
  for members. Guild membership changes refresh the panel.
- GuildRegister shows Create a Guild / Create 1000 fame without membership,
  or the authoritative rank and guild name with Renounce. Creation opens a
  native name frame; renunciation requires the No, I'll stay / Yes, I'll quit
  confirmation. CREATEGUILD/GUILDRESULT and GUILDREMOVE are wired. Rejections
  stay visible and permit retry; successful renunciation follows the server's
  guild-name stat (the server sends no GuildResult for self-removal).
- NameChanger shows Choose Account Name or the current name / Change 1000
  gold. CHOOSENAME/NAMERESULT are wired. This fork never calls
  NameChanger.setRankRequired, so its required rank remains zero. The server
  charges 1000 gold for a changed name even with NAME_CHOSEN false; the
  confirmation frame states that actual cost. Input keeps the Flash limits:
  10 letters for names, 20 letters/spaces for guilds.
- Renames update Session's login name on successful NAMERESULT before a
  RECONNECT. Startup saves the new name with the existing password and private
  file permissions, and uses it when returning to character selection. A save
  failure keeps the new credentials in memory and reports the persistence
  error on return to the menus. Balance, name, guild and rank stats remain
  authoritative; dialogs do not predict them.
- Native frames block gameplay input while retaining network/world updates.
  Pending requests disable submission/cancellation; errors enable retry.
  Press/release picking, focus-loss cancellation, map/reconnect/death teardown,
  shrinking to small windows, and long-title layout are covered. A 20-character
  all-W guild name uses smaller text to fit above the action button.
- One 440x340 (598,400-byte) CPU image/atlas region serves every modal.
  Unchanged panels and frames reuse their bitmaps; 1,000 unchanged checks per
  fixture make no new rebuilds. New panels use existing fonts/currency artwork
  and simple native button shapes, with no new Flash exports.
- `interaction_panels` and `social_loopback` are part of CTest. The independent
  TCP peer verifies social packet bytes, fragmented rejection replies, retry,
  duplicate name submission suppression and renamed HELLO credentials after
  guild-portal RECONNECT. Optional native rendering/cache checks pass in
  Release and ASan/UBSan (10,069 assertions).
- `tests/social_hud_tests.py` runs the real native application against isolated
  TCP/HTTP fixture peers. All eight 800x600/1280x720 scenarios passed: guild
  creation, renunciation, name changes and portal clicks. The name scenarios
  use realm_client's saved-account startup and verify saved credentials,
  owner-only permissions and the renamed /char/list request after Escape.
  Normal-size captures were reviewed under `build/interaction-reference`.
  These are fixture peers, not new live-server purchase/guild verification.
- Measurement: the 800x600, 8x8 guild-creation fixture on Linux/NVIDIA RTX
  3050 Ti with `--benchmark` (vsync off) recorded CPU event/build/submit mean
  0.0513 ms / p95 0.0703 ms over 3,936 samples, presentation p95 0.3085 ms,
  and sampled process VmHWM 212,756 KiB (207.8 MiB). The atlas remained two
  pages / 32 MiB. This small scripted fixture includes screenshot work and
  initialization in peak memory; it is not a dense gameplay benchmark, GPU
  elapsed-time measurement, input-latency measurement or whole-client speedup.

### Native item tooltips (2026-10-10)

- Equipment, inventory, backpack, nearby loot, potion stacks, and merchant
  item icons show original item names/art and readable wrapped descriptions.
  Effects include projectile damage/range/rate, conditions, equip bonuses,
  activations, cooldowns, MP costs, fame bonuses, and use/class restrictions.
- Damage/rate/cooldown/stat modifiers come from ItemData. Matching equipped
  items provide labeled per-shot, range, rate, shot-count, cooldown, and equip
  stat comparisons. Green/red differences describe individual stats rather
  than claiming an overall DPS or upgrade verdict.
- XML DEX/VIT/WIS IDs (28/26/27) differ from this fork's wire IDs (25/23/24).
  Tooltips handle the two mappings separately. Removed containers, changed
  merchandise, tab changes, empty slots, and drags cannot leave stale item
  content. Focus loss/window leave hide the tooltip.
- One 256x580 bitmap is reserved in the existing atlas. Item/comparison
  changes rebuild and upload it; mouse movement only changes the quad
  position. A 1,000-update unchanged-hover check made zero new rebuilds.
  Long content is retained and scrolls through a bounded viewport with the
  mouse wheel; scrolling does not rerasterize the content.
- All 963 imported item definitions produced tooltip content and rendered
  through the existing native fonts/art without additional Flash exports.
  Tests cover real fixture values, modifiers, wrong classes, potion picking,
  backpack/container/merchant routing, changes to comparison gear, window
  bounds, and long-content scrolling. Expanded checks: 3,932 assertions.
- Gameplay screenshots were reviewed at 800x600 and 1280x720 on Linux/NVIDIA.
  Captures: `build/tooltip-reference/gameplay-800.png`,
  `gameplay-1280.png`, `staff.png`, `long-top.png`, and `long-bottom.png`.
  They validate readable native presentation; exact Flash pixels are optional.
- `realm_tooltip_tests` is part of CTest. Optional rendering checks:
  `./build/realm_tooltip_tests build/fonts-client build/hud-art build/tooltip-reference`.
  No live-server purchase changes or whole-client speedup are claimed here.

### Existing HUD and minimap evidence

- Character name (20 bold gray with shadow), XP/fame, HP and MP meters use
  authoritative player stats and typed class caps. Cap/boost colors, negative
  boosts, centered values, clipping and faint boost text follow StatusBar.
- The 200x600 HUD bitmap is retained. Only changed components are rasterized
  and uploaded as atlas subregions; unchanged states do neither. All seven
  fixtures match fresh CPU reconstruction after incremental changes, and GPU
  captures match those CPU images exactly through the overlay pass.
- Four of seven complete panel fixtures match original Flash exactly. Three
  retain 12–14 differing pixels around the first digit "8" and its shadow.
  These small rasterization differences are accepted under the clarified scope.
- Base class portraits, equipment and main/backpack inventory render from
  original finite UI artwork. All 140 assembled source fixtures match exactly
  in the portrait and equipment/inventory regions, across 14 classes and 533
  items. These cover empty slot silhouettes and numbers, cut corners, tier
  tags, dose labels, restricted-use colors and starter ability mana dimming.
  All 140 complete native panels also match fresh reconstruction and GPU
  output after incremental atlas uploads. Dynamic status text differences
  are accepted when readable; these comparisons do not establish complete HUD
  functionality or quality.
- `tools/bake_hud_art.py` exports an indexed 1,333,010-byte PNG bundle with
  1,758 records, including the six fixed stats labels at two horizontal phases.
  Runtime loads requested records only and uses an O(1) LRU
  bounded at 128 images / 4 MiB decoded pixels. Its complete-record stress run
  passed ASan/UBSan, made 2,891 evictions and ended at 128 images / 1,428,224
  bytes while preserving reconstruction. AIR is used only during offline baking.
- Native HUD input supports drag/swap, dropping to the world, double-click
  equip/use, shift-click consumables, ctrl-click backpack transfer, clicking
  the main/backpack tabs, and 1–8 inventory use. Drag artwork uploads only when
  its type/dim state changes; pointer movement changes its quad position.
  Drag visuals still need review at playing size. Focus loss cancels a
  drag and preserves the selected tab. Item tooltips are now implemented above.
- The nearby container panel follows GameSprite.updateNearestInteractive and
  InteractPanel. The strictly closest interactive object closer than 1 tile
  is the target. When it is a Container or OneWayContainer, its ContainerGrid
  is drawn at (14,508). The x is measured from the empty 172-pixel grid, the
  y is the panel offset of 8. Tiles use the inventory backgrounds without
  hotkey numbers, with the same restricted, quantity and tier art. Changes to
  the panel upload only the 200x100 region from y=500, and tab-panel changes
  leave it untouched. All 112 fixtures in `tests/container_hud_tests.py`
  match original Flash exactly in the 300-600 region, on fresh CPU and
  incremental GPU paths. They cover 14 classes, loot bags, gravestones, vault
  chests, gift chests, partial and empty grids, tiers and tab changes. Each
  fixture also asserts its upload regions.
  Input follows ItemGridMediator:
  - Shift-click uses the item.
  - Ctrl-click does nothing.
  - Double-click moves it to the next free inventory slot, or uses it if
    there is none.
  - Dragging swaps with any tile, subject to equipment slot types.
  - Dropping on the tab strip stores it in the next free slot.
  - Dragging a container item to the world sends nothing.
  - A player item dropped on the world swaps into the first empty slot of a
    nearby container. That bypasses the OneWayContainer check, as
    dropWithoutDestTile does. Otherwise it sends INVDROP.
  - Replacing the container cancels drags and pending clicks on its tiles.
  Not implemented yet:
  - MoneyChanger. GuildRegister, GuildHallPortal and NameChanger now have
    native panels/dialogs (see above). Merchant, ClosedVaultChest and
    GuildMerchant purchase panels also exist in the code.
  - The loot_appears sound. Container item tooltips are now implemented.
- The portal panel follows PortalPanel. The name is GameObject.getName (the
  NAME stat, otherwise the display ID); LockedPortal definitions strip one
  "Locked " prefix. It is an 18-pixel bold white field with a shadow, 188
  wide at panel origin (6,508), with y 6, or 0 once taller than 30 pixels.
  Measured from Flash TextLineMetrics:
  - Line widths are the summed advances without trailing spaces, matching
    `FontBook::measure` exactly for all 63 distinct lines.
  - Each line starts at 2 + (184 - width) / 2, floored to twips.
  - Lines wrap after the last word that fits in 184 pixels. A word wider than
    that breaks between characters. `FontBook::wrap` reproduces all 84
    fixtures' line breaks.
  - Lines after the first render 0.2 pixels below k * 20.6. This was found by
    sweeping baked vertical phases on lines two to four. Phases .0/.2 and
    1.0/1.2 render identically, which suggests a quarter-pixel vertical glyph
    grid. The rule is verified only for 18-pixel bold.
  The constant parts come from the art pack, captured from a real PortalPanel
  on stage in panel coordinates: the Enter TextButton (normal and hover) and
  the red Full/Locked fields. Enter shows when the portal is unlocked and its
  ACTIVE stat is nonzero (default true); otherwise Full or Locked shows.
  All 84 fixtures in `tests/portal_hud_tests.py` match original Flash exactly
  in the 300-600 region, on fresh CPU and incremental GPU paths. They cover all
  37 Portal definitions, server realm names with counts, single characters,
  empty, leading/trailing/double spaces, two to four lines, an unbreakable
  word, inactive and locked states, hover and upload regions. The exporter
  also asserts the Enter button rectangle (73,562,54,26), which drives the
  hit test.
  Input:
  - Hover highlights the button while the pointer is inside its cut-edge
    shape.
  - A click needs both press and release on the visible button and sends
    USEPORTAL.
  - The interact key 0 (Parameters default) also sends it, with key repeat
    and even while Full or Locked, as PortalPanel.onKeyDown does.
  The hover, shape and click semantics are inferred from TextButton and Flash
  picking rules, not compared against live Flash.
  Not handled: names go through htmlText, so a name containing `<` or `&`
  would be parsed as markup in Flash but is drawn literally here.
  GuildHallPortal now uses its own native panel (see above).
- The default PartyPanel draws at (6,508) when there is no interactive target.
  `Party` follows Party.update. It refreshes at most every 500 ms, and an
  ACCOUNTLIST forces the next refresh. Members are other players within 50
  tiles (distance squared at most 2500) or starred players, sorted by starred,
  then distance squared, then object ID, keeping at most 6. A star comes from
  ACCOUNTLIST id 0 and stays on that Player object; id 1 replaces the ignore
  list. The party resets on MAPINFO.
  Each member is drawn on the x 0/100, y 0/32/64 grid:
  - The class portrait is drawn at (x-4,y-4).
  - The name is a 13-pixel bold field, 66x20 with the default shadow, at
    (x+32,y+6). Fellow guild members get 10944349, everyone else 16572160.
  - Unstarred members get the 0.6 dark ColorTransform.
  - Members under 20% HP pulse with red offset rv*128, where
    rv = int(|sin(t/200)|*10)/10. Concatenating the dark transform leaves
    the offsets unscaled.
  - Flash skips the identity transform (pulse step 0 on a starred member).
  Flash's ColorTransform on premultiplied pixels is
  clamp(floor((p*m8*255 + offset*a*256)/65280), 0, a), with the 8.8
  multiplier m8. This was measured with an AIR probe over every alpha/channel
  pair for all 37 reachable transforms (`tools/bake_party_transforms.py`).
  The formula is off by one on 15,835 pairs. Those are stored as a 63 KiB
  sorted constant table (`assets/party-transforms.bin`, generated into
  `flash_party_transforms.hpp`). Any unmeasured transform throws.
  `FontBook::rasterize` now accepts a fixed field height.
  40 of the 41 fixtures in `tests/party_hud_tests.py` match original Flash
  exactly in the 300-600 region, on fresh CPU and incremental GPU paths. They
  cover:
  - all 14 classes;
  - empty and solo parties, ordering, the distance limit and ties;
  - guild colors;
  - all 10 pulse steps on dark and starred members;
  - zero, over-max and negative HP;
  - redraw regions;
  - a portal hiding the party.
  The remaining fixture, the bold 13-pixel name "Dying", keeps a pinned
  two-pixel difference from the known glyph-overlap coverage issue (see
  Fonts in progress). `realm_party_tests` covers the throttle, sticky stars,
  removal, the ignore list, the cap of six and reset.
  Not implemented: PartyPanel hover (which freezes the list), PlayerToolTip,
  the PlayerMenu, and acting on the ignore list.
- The ButtonPanel family covers GuildBoard ("Guild Board" / View),
  GuildChronicle ("Guild Chronicle" / View), CharacterChanger ("Change
  Characters" / Change) and ReskinVendor's ReskinPanel ("Change Skin" /
  Choose).
  - The title is drawn natively with the PortalPanel field: 18-pixel bold,
    188 wide, centered, with a shadow, at y 6.
  - ReskinPanel never sets its title's y, so its title is at 0. It also sets
    htmlText after multiline, so Flash reports an extra empty second line,
    which draws nothing.
  - The TextButtons are captured from Flash without titles, normal and hover,
    as eight additive art records (1,776 total). The 1,768 earlier records are
    byte-identical, and the previous pack is `build/hud-art-before-buttons`.
  - Measured button rectangles: View (74.5,562,51,26), Change (65,562,70,26)
    and Choose (66,562,68,26). `HudRect` keeps the half pixel for hit testing.
  All 28 fixtures in `tests/button_hud_tests.py` match original Flash exactly
  in the 300-600 region, on fresh CPU and incremental GPU paths. They cover:
  - every object of the four classes, with and without hover;
  - a rotation through all 14 player classes, including stats-tab cases;
  - an unchanged redraw and hover-only upload regions.
  The exporter also asserts the title lines and positions and every button
  rectangle.
  Input:
  - Portal and button panels share `HudState::panel_button` and
    `HudInput::button_down`/`button_up`.
  - A click needs the press and release on the same target's button; a
    replaced panel cancels it, and so does focus loss.
  - Change sends the game back to character selection, the native equivalent
    of GameSprite.closed, as Escape does.
  - The guild board, guild chronicle and reskin windows are not implemented,
    so View and Choose do nothing yet.
- A bold 13-pixel stats value ending in a bare "4" (for example DEF 24 or
  WIS 334) differs from Flash by about 7 pixels at the field's clip edge.
  The "4" crossbar ends inside the edge pixel but past the clip line; Flash
  clips the outline before antialiasing, so it draws no ink there. The
  current model takes the glyph alpha up to the clip coverage, which keeps
  some ink. A 720-field probe across both reachable stats phases, every final
  digit and eight boosts found only these 8 cases
  (`tools/stats_clip_probe.py`). Snapping the clip edge to whole pixels
  breaks dozens of other fields. The stats suite has no such values. This is
  an accepted minor rasterization difference; do not pursue subpixel glyph
  reconstruction solely to match Flash.
- A null-reference bind in `HudState::player` with no nearby container,
  found by UBSan, is fixed.
- Potion widgets (PotionInventoryView) draw on the main and backpack tabs
  from stats 59/60. Each widget has a stocked or empty cut-edge background,
  a fixed 24-pixel size-13 count field with a strength-2 shadow, and a
  46x35 icon at (13,-11) above the lower grid row. Their y is 468 because
  TabStripMediator measures the empty grid (84 pixels) before items are set;
  measuring after items would give 92-100. All 126 fixtures in
  `tests/potion_hud_tests.py` match original Flash exactly in the 300-500
  region: 14 classes, counts 0-6, 10 and 123, both tabs, and items under the
  icons. They also match fresh reconstruction and incremental GPU uploads.
  Six records were added to `build/hud-art` (1,764 total); the previous pack
  is `build/hud-art-before-potions` and its 1,758 records are byte-identical.
  The source never clears PotionModel.available, so the grayscale state is
  unreachable and not drawn.
- Potion input follows PotionSlotView and UsePotionCommand. Shift-click or a
  second MOUSE_UP within 250 ms on the same view uses a potion; leaving the
  view, tab changes and focus loss cancel the pending click. The hit area is
  the background shape plus the icon bounds, with the magic view topmost.
  The non-mouse-enabled count field is excluded. This is inferred from
  Flash picking rules and has not been compared with live Flash. F and V
  include key auto-repeat, matching KEY_DOWN. Nothing is sent with an empty
  stack or a full HP/MP meter. USEITEM sends the player position, predicts
  the stack decrement and, on rejection, restores both stacks captured
  before sending, in FIFO order with other inventory operations.
  The use_potion sound is pending with audio. Keys 1-8 still ignore
  auto-repeat, unlike the source.
- FontBook accepts a shadow strength and a fixed TextField width. The
  defaults reproduce the previous output exactly.
- Stats tab draws authoritative ATT/DEF/SPD/DEX/VIT/WIS values and boosts with
  original gray/green/red/gold priority and class caps imported once from XML.
  Click selects all available tabs; B cycles them, matching the source default.
  Tab changes cancel drags and double-click history. Equipment remains usable
  while the stats body has no inventory slots. An absent backpack cannot be
  selected, and release over a tab without pressing it does not select it.
- All 140 stats/tab fixtures across 14 classes match original Flash exactly in
  the equipment/tab region, including zero defense, capped/over-cap values,
  positive/negative boosts, varying text widths and inventory/backpack returns.
  Retained panels match fresh CPU reconstruction and incremental GPU uploads.
  A stats-only change uploads the 200x127 content region; hidden stat changes
  cause no rebuild or upload. References: `build/stats-reference/comparison.json`.
- Current font/HUD packs are installed at `build/fonts-client` and
  `build/hud-art` (1,768 records; the four new portal records are additive
  and the 1,764 earlier records are byte-identical). Previous versions are
  retained in `*-before-portal`, `*-before-potions` and `*-before-stats`.
- Inventory prediction moves/clears both item type and ItemData. INVRESULT
  restores rejected operations in send order, including ability USEITEM in
  the same FIFO. Successful operations retain prediction until server stats
  update it. Missing container rollback and map/reconnect reset are handled.
  Inventory use preserves authoritative HP, MP, conditions and equip bonuses.
  Container pickup, swap and ground-drop routing are wired to the interaction panel.
- Minimap implements remembered ground/static occupying objects, unknown
  black tiles, source zoom levels and edge clamping, player/guild/enemy/portal
  markers, camera arrow, and zoom controls. +/- and clicking the original
  zoom button positions work. Static object drops leave their remembered
  pixels until a ground update overwrites them, matching the source signals.
- World tracks minimap row revisions. Only changed rows are recolored; a
  changed camera/marker view reuses the map colors. Unchanged views avoid
  rasterization/upload. Marker vectors reuse capacity. Source PNG copies are
  released after the small texture-color cache has been populated.
- All 2,776 source ground/object colors and 24 unrotated minimap frames match
  exactly across three map sizes, four zoom requests and center/edge focus.
  Rotated arrow comparisons retain 3–8 differing pixels in each failing
  fixture; these tiny arrow differences are accepted. Outside-minimap marker
  overflow, full HUD scaling, alternate static object appearance colors,
  minimap player menus/tooltips and deactivation
  still need implementation or verification.
- Source getPixel32 recovery was measured over all 65,536 gray/alpha samples,
  covering 32,642 reachable internal pairs. The resulting 64 KiB constant
  lookup preserves differences from PNG export unpremultiplication.
- HUD and minimap use two quads in one overlay draw call after world scissoring;
  the Nexus capture contains 369 quads, two total draws and two atlas pages.
  Screenshots: `build/hud-reference/offline-minimap.png` and the new
  `build/hud-reference/offline-inventory.png`. The new capture includes the
  base Wizard portrait, starter equipment and inventory. This is still a
  development harness, with interaction views absent.
  `build/hud-reference/offline-potions.png` adds the potion widgets.
- All six atlas galleries remain exact after overlay renderer changes, and
  the 24 basic text fields remain exact on CPU and GPU.

## Startup sequence

`realm_client` (or `realm_viewer --client`) follows the Flash flow:
- `StartupSequence` shows `AccountLoadingScreen`.
- `WebLoadAccountTask` verifies the saved account through `/account/verify`.
- `GetCharListTask` loads `/char/list`. Rejected credentials clear the account
  and retry immediately; other errors show "Load error, retrying" and retry
  after 1 s.
- Then `TitleView`, `CharacterSelectionAndNewsScreen`, `NewCharacterScreen`,
  and finally `PlayGameCommand` arguments handed to `Session`.

Pieces:
- HTTP: `net::http_post` in realm_core.
- Char-list parsing, Flash's character sort (most recently used, then
  experience), fame stars and the saved account: `account.hpp`. The saved
  account is the SharedObject equivalent, stored as JSON with owner-only
  permissions.
- Screens and dialogs: `startup.hpp`. Menus render through one reserved
  1024x1024 atlas image as window-space overlay quads.
- MenuBackground: the embedded JM map, written twice with a 10-tile border
  (`World::load_jm_text(..., wrap_border)`). It is drawn through the live
  terrain and scene with a fixed-clip camera (`Camera::configure(position,
  angle, clip)`), scrolls at 1 tile/s and is dimmed by 0x2B2B2B at 0.8.

Verified against an isolated real server with `--script` at 800x600 and
1280x720:
- guest boot, title, guest character list and class grid;
- the register dialog, Rogue creation, the Nexus;
- Escape back to the list with the new character, entering again;
- a second launch from the saved account.

Deviations and gaps:
- Guests are asked to sign in or register when they press Play, because the
  server rejects empty-credential HELLO packets. Flash would try to play as a
  guest.
- The `CharacterSkinView` step is skipped (default skin).
- Character, class and New Character icons scale the baked HUD portrait
  (4x sprite) to Flash's 5x/6x redraw sizes. They are static (no
  CharacterBox posing) and show no dyes.
- Not implemented (these show a message instead): Legends, the map editor,
  buying slots, character deletion, star and credit graphics, news icons and
  links, the list scrollbar (the mouse wheel scrolls the list), and the death
  screen (a "Your character has died." message instead).
- Under sdl2-compat, pushing synthetic `SDL_TEXTINPUT` events crashes. Scripted
  text is therefore handed to the menus directly. Real keyboard text input
  uses SDL text events.

## Correct source behavior worth preserving

- Local bullets use negative signed 32-bit IDs. Parametric motion uses signed
  remainder, including negative IDs; enemy/server shot IDs are full int32.
- Incoming status stat counts are bytes. String stats are NAME and GUILD_NAME;
  ACCOUNT_ID is numeric. This fork has no legacy packet cipher.
- Remote interpolation is 127 ms. Duplicate target ticks preserve velocity.
- MOVE is sent once per frame while ticks are pending, after local movement;
  sinking/ground effects update only after that acknowledgment.
- `GameObject.damage` displays feedback and does not subtract HP or set
  conditions locally. Server stat packets own HP and conditions. Damage cues
  now record immediate feedback for the forthcoming text/effect renderer.
- Original bitmap textures are premultiplied in GPU storage and the existing
  Stage3D shader blends with SOURCE_ALPHA. Colored translucent pixels can
  therefore be darker than conventional straight-alpha rendering. Preserve
  recognizable appearance. This records Flash's behavior; native blending
  may be simplified or improved under the current visual quality goals.
- A raw translucent PNG channel decodes to floor(channel*alpha/256), except
  opaque channels remain unchanged. PNG export unpremultiplies with a 256
  scale; its exact inverse is ceil(channel*alpha/256), except opaque channels.
- Outline projectile texture pixels stay at native pixel scale and get
  clipped by a rotated square; they are not fitted into the square.

## Remaining systems

- Complete HUD widgets, readable text/layout, names, status text/icons, player
  and other non-item tooltips, chat and remaining minimap interactions/edge behavior.
- Title, login, registration, character selection and creation are
  implemented (see Startup sequence). Remaining: settings, death/fame
  screens, legends, purchases, skins, character deletion, trading, guilds and
  the other exposed screens.
- Basic ItemData modifiers, abilities, escape, potion stacks and inventory
  prediction/rollback and the nearby container panel are implemented.
  The nearby portal panel and USEPORTAL, default PartyPanel, ButtonPanel family,
  native item tooltips, and merchant/closed-vault/guild-merchant purchase panels
  are implemented, as are GuildRegister, GuildHallPortal and NameChanger with
  their creation/renunciation/name dialogs. Remaining: MoneyChanger; the guild board,
  guild chronicle and reskin windows; party hover/tooltip/menu; and complete
  teleport/actions and their UI.
- Masks/dyes, arbitrary live appearance sizes and colors, bounded/lazy
  processed texture cache. Existing processed atlas loads all supplied PNGs.
- Real 3D models, particles, blood/trails, environment/background lighting,
  post effects, breath/boost bar details and complete overlay ordering.
- Audio/music/sound routing, focus/pause/input persistence and reconnect UI.
- Deterministic full-scene replay/reference captures and matched Flash/native
  CPU/GPU/memory measurements. Current timing figures are isolated harness
  results and must not be presented as a whole-client speedup.
- Packaging, cross-platform builds and runtime validation outside Linux.

## Next work

Prioritize completing usable gameplay and finding representative performance
bottlenecks. Continue party hover/PlayerToolTip/PlayerMenu and missing
guild/reskin windows. GuildRegister/GuildHallPortal/NameChanger panels and
their native dialogs are implemented. Merchant purchases are wired, but
BUYRESULT feedback and MoneyChanger still need review. Native item tooltips are implemented. Check current
code before selecting work; this evidence log may lag implementation. Stats,
tab switching, potions, containers, portals, buttons, and the default party
panel already have substantial validation.

Review new UI and playfield work at normal playing size for familiar appearance,
clear text, correct interactions, and stable rendering. Fix missing content,
significant clipping, and misleading gameplay cues. Leave tiny font, shadow,
color-transform, and rotated-minimap differences accepted. Do not extend Flash
coverage probes, phase packs, or transform correction tables solely for exact
matching; prefer simpler native implementations where useful. Existing tooling
can remain as optional diagnostics.

Measure CPU/GPU frame times, memory, loading/map changes, and responsiveness
alongside feature work. Keep meaningful protocol, functionality, memory safety,
cache, and native rendering checks. Runtime must remain native; AIR may be used
offline. Routine implementation and isolated fixture accounts need no new
permission.
