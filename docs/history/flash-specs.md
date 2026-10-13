# Flash UI specifications pending native implementation

Extracted from `realm-client` and `realm-server` sources on 2026-10-10 for
features not yet ported. Paths are relative to `realm-client/src` unless
noted. Verify against source before relying on exact values.

## Dialog (implemented in task 01-dialogs; com/company/assembleegameclient/ui/dialogs/Dialog.as)

- Box 300 wide, fill 0x363636 alpha 1, 1px white outline, 4px cuts on all
  corners, DropShadowFilter(0,0,0,1,16,16,1). Height = content + 10.
- Title SimpleText 18 bold 0x57AD62, centered in 300, y 2, shadow (0,0,0,1,8,8).
- Body SimpleText 14 0xB3B3B3, centered, wrap 260 at x 20; y = title bottom
  + 8 (4 without title); shadow (0,0,0,1,6,6).
- Buttons TextButton(16,label,120): one at x 90; two at x 15 and 165;
  y = body bottom + 16. TextButton: text 0x363636 bold at y 1,
  x = w/2 - textWidth/2 - 2; height textHeight+8; fill white, hover
  0xFFDC85, disabled 0x7F7F7F; 4px cuts.
- Scaled by stageHeight/600 and centered using outline-inclusive bounds:
  301 x (frameHeight+1). No backdrop of its own; OpenDialogSignal's enclosing
  `kabam/rotmg/dialogs/view/DialogsView.as` supplies 0x151515 at alpha 0.6.
- Title uses fixed width without automatic wordWrap (explicit newlines work).
  Body autoSize CENTER retains fractional textHeight+4. Button y and frame
  height are integer-rounded from box bounds; see real exporter metrics in
  `build/dialog-reference/flash/metrics.json`.
- MessageCloseDialog: one button, "Close" by default. BUYRESULT result 1
  opens MessageCloseDialog("Purchase Error", text).

## Guild board (implemented in task 02-guild-board; ui/board/)

- GuildBoardWindow: black backdrop alpha 0.8; Dialog "Loading..." /
  "Saving..." while requests run; board centered at (400 - w/2, 300 - h/2).
- ViewBoard: SimpleText 16 0xB3B3B3 masked to 400x400 at (0,0); URLs in
  #7777EE links; frame fill 0x333333, 2px white stroke,
  drawCutEdgeRect(-6,-6,w+12,h+12,4). Scrollbar(16,396) at x 406 only when
  text is taller than 400 (w 426, else 400). Edit (officers, rank >= 20) at
  (4,404), Close at (w-124,404), TextButton 14 width 120. h = 400 + button + 8.
- EditBoard: input field (border=false overrides SimpleText input default, no fill) 400x400, scrollbar
  always, Cancel at (4,404), Save at (302,404). No max length.
- Server (`realm-server/Networking/AppServer.Social.cs`): POST form fields
  `username`, `password` (and `board`). `/guild/getBoard` returns raw text;
  `/guild/setBoard` needs GuildRank >= 20 and returns the stored text.
  Errors are `<Error>...</Error>` ("Invalid account.", "Not in guild",
  "No permission").

## Guild chronicle (implemented in task 03-guild-members; ui/guild/)

- GuildChronicleScreen: backdrop 0x2B2B2B alpha 0.8; MenuFrame (bottom bar
  0x545454 at y 524 h 52; black gradient glow y 508 h 80, alphas
  0/.73/.73/0 at ratios 20/103/173/237); TitleMenuOption "continue" 36 bold
  white, shadow (0,0,0,.5,12,12), hover ColorTransform(1,220/255,133/255),
  centered at y 520.
- GuildPlayerList: "Loading..." 22 bold 0xB3B3B3 centered; title (guild
  name) 32 bold 0xB3B3B3 y 24 centered; fame 22 white right edge 768 with
  40px guild fame icon (lofiObj3 226) at x 760; 2px 0x545454 line at y 100;
  list mask (10,110) 756x430; Scrollbar(16,400) at (780,104) when needed;
  "N open slots" 22 0xB3B3B3 centered, N = 50 - members.
- MemberListLine 756x32: 22pt with shadow (0,0,0,1,8,8), 0xB3B3B3 (own row
  0xFCC219). "N." bold right edge 60; name bold x 100; fame right edge 408
  with 40px icon at 400; rank icon 20px at 548 (lofiInterfaceBig 20/19/18/
  17/16 for ranks 0/10/20/30/40); rank text x 580; promote/demote white
  triangles at (676,16)/(706,16); remove X at 730. Confirmation Dialogs
  "Promote X"/"Demote X"/"Remove X" with Cancel + action button.
- Server: `/guild/listMembers` (username, password) returns
  `<Members><CurrentFame/><GuildLevel/><Member><Name/><Rank/><Fame/></Member>...</Members>`
  in stored order. CHANGEGUILDRANK(name, rank) and GUILDREMOVE(name) reply
  through chat lines (SendGuild / SendError), not GuildResult. Rank changes
  need actor rank >= 20, actor > target, actor >= new rank, new != 40, same
  guild. Flash permissions are stricter: officers cannot promote members to
  officer; leaders may promote officers to leader; nobody may promote to founder.
- The current server omits Members.@name: native uses the current player's guild.
  Members remain in stored order, with no fame/rank sorting. Source list bounds
  start at -4 from the first 40px fame icon; scroll range uses full list height
  minus 400, although the mask height is 430.
- Native matches confirmation strings and source Error/Ok, refreshes on matching
  TEXT replies, and adds recoverable HTTP Retry/Close and confirmation timeout.
  The source waits for GUILDRESULT here, which this server never sends.

## Scrollbar (ui/Scrollbar.as)

- Arrow height int(w*0.75) (12 for 16); track 0x545454 from arrow+5,
  height H - 2*arrow - 10; white arrows (-6,-8),(6,0),(-6,8) rotated ∓90 at
  (W/2,6) and (W/2,H-6); white 4px-cut thumb, height between W and the
  track; hover tint (1,.8627,.5216). Wheel: 0.2 of a page per notch; track
  click jumps a page; arrow hold scrolls (H - 3W)*speed px/s.

## Minimap player tooltip and PlayerGroupMenu (implemented in task 04-minimap-menu)

- kabam/rotmg/minimap/view/MiniMap.as: while hovered, Players (not the
  local player, not noMiniMap) within 5 minimap pixels of the cursor
  (off-window players clamped to the edge) form the hover list; the menu
  must be closed. A PlayerGroupToolTip follows the mouse (ToolTip.position
  rules: right/below in left half/top third, else left/above, min 12).
  Click replaces any menu with PlayerGroupMenu at the cursor.
- PlayerGroupToolTip: fill 0x363636 alpha 0.5, white outline, cut frame at
  -6 with 6px padding, 16px halo; GameObjectListItem rows 32 apart; footer
  "(Click to open menu)" 12 0xB3B3B3 centered at y 32*N.
- PlayerGroupMenu: Menu frame (opaque 0x363636); TeleportMenuOption at
  (8,8) when the map allows teleport; LineBreakDesign(width-24, 0x1C1C1C) at
  (6,40) (2px bar with 4px diamonds at both ends); rows from y 52 (4 without
  teleport) 32 apart, not interactive. Teleport picks the first visible
  player in list order, else chat *Error* "No players are eligible for
  teleporting".
- PlayerArrow (map/partyoverlay/; implemented in task 05-party-arrows): edge-of-view arrows for party members
  that are not drawn; 8px (11px hovered/big) triangle, white outline, fill
  0x3FC752, shadow (0,0,0,1,8,8); arrows within 8px merge. Hover shows
  PlayerGroupToolTip; mouse down opens PlayerGroupMenu and stops the shot.

## Reskin (kabam/rotmg/characters/reskin/)

- ReskinPanel "Change Skin" / Choose opens ReskinCharacterView ("Select a
  Skin", 0xB6B6B6 18; Cancel/Select 16 width 120). RESKIN(skinID) is checked
  by the server against owned skins and class (skin 0 resets).

- Skin definitions come from EmbeddedData_SkinsCXML.dat, explicitly loaded
  outside the objectFiles registry: 15 skins across 14 classes. Classic is
  always first; remaining rows preserve source order and class filtering.
- ReskinCharacterView is 462px wide; list width 442, rows 60 high with 5px
  gaps at (10,37), height min(410,list height)+87. Divider is 2 physical
  pixels (source LineScaleMode.NONE). Radio selection uses original geometry.
- CharacterSkinView/ClassDetailView show walking preview (200ms), hover
  preview after 250ms, source description, class stars/best level/fame,
  next goal and account currency. Unowned rows expose Buy; native current
  credentials/configured host are used for account/purchaseSkin.
- Source error dialogs are Not Enough Gold and D'oh, this isn't good,
  including the original spelling An error has occured. Modal errors replace
  the skin chooser. Resize cancels stale button presses.
- Normal in-game chooser uses cached startup OwnedSkins. The current server
  rejects both char/list and purchaseSkin while online (Account in use!).
  Successful offline purchases persist ownership/credits; asynchronous receipts
  follow their account and cannot leak to a different login.
- Native RESKIN validates ownership/class locally, sends the existing int32
  packet and waits for server texture stat 70 before changing appearance.
  Live skin portraits refresh in HUD, party, tooltips, menus and grouped UI.
  Player/class labels and ordered ID/data snapshots retain source semantics.
- Task 06 uses separate original skin UI art plus neutral size-100 animation
  frames. Tex1/tex2 dyes, masks and other encountered appearance sizes are
  task 07; inspect Flash texture composition before extending the cache.


## Dyes and masks (task 07)

- `util/TextureRedrawer.as`, `TextureRedrawer_textureShaderEmbed_.dat`:
  masked dye composition enlarges the source five times before applying the
  shader; `resize` then divides the integer size by five. Mask red selects
  tex1, green selects tex2; the chosen channel shades the selected RGB. A
  zero operation leaves the source pixels. Unmasked frames retain their art.
- Operation high byte 1 selects the low 24-bit solid color. High bytes 4, 5,
  9 and 10 index the corresponding textile image sets. Patterns repeat from
  the enlarged frame origin, including mirrored/extended attack padding.
- `objects/TextureData.as` retains explicit static Mask and animated frame
  masks. Native imports both. Source `ObjectLibrary.getRedrawnTextureFromType`
  uses item XML Tex1/Tex2 for inventory, tooltip and merchant icons.
- Stat 29/30 update authoritative dye IDs; stat 2 changes size; stat 70 changes
  skins. World and live HUD/party/tooltips/menu/group portraits must refresh.
  Portraits use the right-standing frame, size int(400/frame width), bottom
  padding and source glow. Identity/order/labels remain owned snapshots.
- `objects/Player.as::getNearbyMerchant` checks four ordered nearby cells and
  strict squared distance < 1. `Merchant.getTex1Id/getTex2Id` previews sold
  Dye values only on the local world sprite, preserving authoritative dyes
  and portrait values. Merchant world images display the sold item.
- Native lazy appearance caches have count and byte bounds. Eviction must
  preserve already-built quads until submission; map generations clear both
  caches. Native gradient/halo and nearest boundary rounding may differ
  slightly from Flash, within the current at-a-glance contract.

## Particles and direct effects (task 08)

Inventory verified against the actual current registrations, effect classes,
`GameServerConnection.onShowEffect/onAoe`, `GameObject.damage`, `Projectile`
and `Player` (2026-10-11). Network IDs stay unchanged. SHOWEFFECT carries a
byte type, target ID, ARGB color, first position and optional second position.
The color's alpha is ignored by solid particles, as in redrawSolidSquare.

| Type | Actual source event and appearance |
| --- | --- |
| 0 | Unknown; no effect. |
| 1 Heal | Ten following colored particles at radius .3–.7, sizes 60–140, rising for 1000ms. Requires target. |
| 2 Teleport | Twenty blue particles, radius 0–.7, sizes 50, rising for 500–1500ms at pos1. |
| 3 Stream | Five sizes 60–140 at z1.85, sinusoidal deflection between the positions, 1500–4500ms. |
| 4 Throw | Arcing spark trail from target to pos1. Default 1500ms; this fork uses optional pos2.x as milliseconds, clamped 100–10000. Missing target: pos2 is start for old packets; pos1 for custom-duration packets. |
| 5 Nova | 4+2*radius outward sparkers, size200, 200ms flight, 600ms debris. Radius=pos1.x; target is center. |
| 6 Poison | Ten size100 sparks, z.75, ±4 speed, 400ms. Requires target. |
| 7 Line | Thirty size100 sparks between target/pos1, ±1 speed, z.5, 700ms. |
| 8 Burst | Twenty-four size100 outward sparkers, center=pos1, radius=distance(pos1,pos2), 100–300ms flight. Requires target. |
| 9 Flow | Five sizes60–140 from pos1 accelerating toward the moving target; disappear within .5 units. |
| 10 Ring | Twelve sparkers from .9*radius to radius, size0 driver, 200ms flight. Their emitted sparks remain visible. Target center, radius=pos1.x. |
| 11 Lightning | Three sparks per unit, z.5, size=pos2.x, tapered jitter and 100–1000ms lifetimes. Target/pos1 endpoints. |
| 12 Collapse | Twenty-four size300 sparkers from edge to pos1 center, 200ms flight. Radius=distance(pos1,pos2); requires target. |
| 13 ConeBlast | Seven size200 sparkers over PI/3 aimed at pos1; radius=pos2.x; 100ms flight. Requires target. |
| 14 Jitter | Camera-only random displacement builds to ±.5 over 10000ms and persists until map replacement. |
| 15 Flash | Target sprite RGB transform, sine pulse with half-strength color bias. pos1.x=period seconds, pos1.y=repeats. |
| 16 ThrowProjectile | Color field is object type; original size80 outlined sprite travels pos2→pos1 on a 1500ms arc, bitmap fill rotates inside its fixed square. |

AOE uses Nova geometry at packet position/radius/color independently of its
hit acknowledgment. GameObject.damage creates ten nonfatal particles even for
zero damage: directional HitEffect with projectile angle/speed, otherwise an
ExplosionEffect. Colors are weighted by nontransparent source pixels and XML
BloodProb/BloodColor. Transparent source textures create no blood. Cover impacts
create three texture-colored particles for friendly projectiles. ParticleTrail
creates three magenta size100, z.5, ±3 speed, 600ms sparks. Player level increases
create two twenty-particle green swirls for 2000ms. Player Healing condition
creates one rising white particle every 500ms; condition removal stops emission.

`Particle` converts size to int(size/100*5); redrawSolidSquare has eight pixels
of padding and a thin black outline. Particle billboards are centered; XML
particles are bottom anchored. Draw order merges particles with GameObjects by
ground-space sort value and fake ID, after shadows and before top tiles.

Four root texture effects are actually registered: White Fountain (20/s blue
parabolic drops), Ocean Vent (20/s rising cyan particles), Hexxer and DeHexxer
(custom rate20/s, green/blue, size5, ~450ms). EffectProperties truncates rangeX/Y
1.9 to integer1. Several unregistered dungeon data files define SwirlingMist
XMLEffects; they are not loaded object definitions in this build. The standalone
particle asset defines SwirlingMist (lofiParts, two frame sequences, z.2, size100,
3s); native import/simulation/rendering supports it. Gas/Bubbles/Healing/XML/custom
emitters have native implementations; Gas/Bubbles have no registered objects.

Native uses an independent visual RNG and fixed particle storage, leaving
combat RNG, HP, MP, condition bits, collision and acknowledgments authoritative.
Particle/sparker/trail emission is limited to approximately 60Hz, with at most
three catch-up emissions after a hitch. Floods drop excess visual particles.
Normal shapes, sizes, colors, movement and layering remain recognizable; random
locations and density can differ from a Flash client running at a different FPS.

## 3D models (task 09)

Verified against `kabam/rotmg/stage3D/{Renderer.as,Object3D/*}`,
`engine3d/{Model3D,Object3D,ObjectFace3D,Lighting3D}.as`, `objects/{GameObject,SpiderWeb}.as`
and `kabam/rotmg/assets/{EmbeddedAssets.as,EmbeddedAssets_*Embed_.dat}` (2026-10-11).

- 42 text OBJ files are registered in `EmbeddedAssets.models_` by name (spaces
  included, e.g. `Candy Col Whole`). Object XML `<Model>` selects one; 40 distinct
  names are used by the registered object XML. Native compiles them at build time
  (`tools/compile_models.py` → `flash_models.hpp`) with the Stage3D parser's rules:
  a new group on `g` and on a changed `usemtl`; faces fan-triangulated as
  (i, 0, i+1); one flat face normal from vertices 0, 1 and last; `Solid*` material
  groups are flat colored; missing UVs read as texture-top (1).
- Placement: `GameObject.moveTo` calls `setPosition(x, y, 0, XML Rotation)`: rotate
  about +Z by degrees, then translate. Model units are world tiles; vertex z is
  height (screen y moves up 50 px per unit). No sort offset: models sort like every
  other object by integer screen y, then object ID, in the ordinary painter stream.
- Shading: `0.75 + 0.25 * max(0, n . L)`, `L = normalize(1, 3, 2)` in world space
  after rotation, applied to RGB only. Textured groups multiply the object sprite;
  `Solid*` groups and objects with no texture use the XML `Color`.
- Culling and depth: Flash draws with the depth test off. Back faces are removed
  (software rule: skip a face when the screen cross product of
  (v1-v0) x (vlast-v0) is negative); remaining faces are painted in file order.
  Native follows this, so model-to-model and model-to-character occlusion is the
  painter order and models are not depth buffered.
- Texture: UVs follow SketchUp tiling and routinely leave 0..1 (for example the
  Ghost Ship Crate top). The default CPU renderer repeats the sprite; the Stage3D
  program samples `clamp`, which smears edge texels into streaks on those faces.
  Native tiles the sprite with nearest sampling, matching the default renderer.
- A model object draws no 2D sprite, name, or health bar. Shadows, tile effects
  and everything else keep their ordinary passes.
- `SpiderWeb` (Web model) stays hidden until a Wall occupies the west, north, east
  or south neighbor square (checked in that order, positions truncated like AS3
  `int`). It then turns 0, 90, 180 or 270 degrees (west keeps XML Rotation), and
  keeps that result (`wallFound_`) until the map is replaced. Flash updates only the
  CPU object's rotation here, and its GPU path draws the model without checking walls;
  native follows the CPU behavior, which is the evident intent.
- Thin models: Gate, Gate Entry and Gate Entry 2 are 0.016-unit plates in the
  Y-Z plane, so at Rotation 0 an orthographic camera sees them edge-on and they
  are effectively invisible in Flash as well.

## Lighting, backgrounds and post effects (task 10)

Verified against `map/{Map,HurtOverlay,GradientOverlay}.as`, `background/*.as`,
`kabam/rotmg/stage3D/Renderer.as`, `engine3d/Lighting3D.as` and `MapInfo.as`
(2026-10-11).

**There is no per-map lighting.** No ambient color, darkness, fog, bloom or light
object exists in this client, and MAPINFO carries no lighting field. The only lighting
is the fixed model shade (task 09: `0.75 + 0.25*max(0, n.L)`, L = (1,3,2)/sqrt(14)).
The "environment" visuals are the following.

- **MAPINFO background** (`int`, after `fp`): 0 none, 1 `StarBackground`, 2
  `NexusBackground`; drawn first, under the tiles, at the map sprite origin
  (the camera point). Every world in `Worlds.xml` sends 0, so the layers are
  implemented for completeness.
  - Stars: 100 attempts, each at (rand*1000-500, rand*1000-500), scale
    `4*(0.5+0.5*rand)`, one of the ten 5x5 images of the `stars` set; any star within 3
    of an earlier one is dropped. Positions rotate by `-angle`; stars stay upright and do
    not scroll with the player.
  - Nexus: 1024x1024 `perlinNoise(1024,1024,8,seed,stitch,fractal,BLUE,gray=false)`, tiled
    over the clip rect with matrix `translate(t*0.01, t*0.01)` then `rotate(-angle)`
    (nearest). The island list is never filled, so only the water shows.
- **Status filters** on the map sprite only (HUD and overlays unfiltered), by player
  condition: Drunk (`DRUNK_BIT`, 1<<8) `BlurFilter(d,d)`, `d = 20 + 10*sin(t/1000)`; Blind
  (`BLIND_BIT`, 1<<6) `ColorMatrixFilter`: every channel is `0.05*(r+g+b)`, and the alpha row
  is `0.05*(r+g+b) + a` (clamped; opaque pixels are unaffected). Software path applies both in that order. The Stage3D path
  (`GPURender`, off by default) instead renders the scene to a texture: blind is the same
  dot product, drunk is a lens warp `uv' = c + (uv-c)*(|uv-c|/0.4)^(f-1)` about
  c=(0.4,0.6) with f swept 0.2..1.8 by 0.01 per frame. Native follows the default
  software behaviour, consistent with task 09.
- **Breath overlay** (`HurtOverlay`, shown while `0 <= breath < 20`, BREATH_STAT 58):
  radial gradient, ratios 0/155/255, alphas 0/0/0.92, over an ellipse through the corners
  of the map viewport, tinted by `ColorTransform(1, 55/255, 0, 0)` with
  `alphaMultiplier = (20-breath)/20 * |sin(t/300)| * 0.75`. The player's breath *bar*
  is the world-space bar described in the status section below. The server never sets `Oxygen`, so no real world shows this today.
- **Sidebar gradient** (`GradientOverlay`): while a player exists, a 10 px black linear
  gradient (alpha 0 -> 1, left to right) at `x = clipRect.right - 10`, full height.
- Quality (`Options` High/Low) only changes `stage.quality` for UI; it has no effect on
  any of the above. Stage3D back buffer MSAA is a render-resolution matter, not lighting.

Native: `post_effects.hpp` (rules), `backdrop*.cpp`, the `Renderer` post pass.

## Status effects, names and floating text (task 11)

Verified against `util/ConditionEffect.as`, `util/redrawers/GlowRedrawer.as`, `objects/{GameObject,Player}.as`,
`map/mapoverlay/{CharacterStatusText,MapOverlay}.as`, `GameServerConnection.as` (onNotification, onNewTick)
(2026-10-11).

- **Conditions.** `ConditionEffect.effects_` is indexed by ID; the CONDITION stat bit is `1 << (ID - 1)`
  (Quiet 1 ... Slowed 3, Sick 4, Dazed 5, Stunned 6, Blind 7, Hallucinating 8, Drunk 9, Confused 10, Stun Immune 11,
  Invisible 12, Paralyzed 13, Speedy 14, Bleeding 15, Healing 16, Damaging 17, Berserk 18, Stasis 19, Stasis Immune 20,
  Invincible 21, Invulnerable 22, Armored 23, Armor Broken 24, Hexed 25). `include/realm/conditions.hpp` is the native table.
- **Condition icons** (`GameObject.drawConditionIcons`): `lofiInterface2` 8x8 frames, one per active bit with an icon, ascending
  by bit (none for Stun Immune, Invisible, Stasis, Stasis Immune, Invincible). Weak cycles frames 34-37 and Paralyzed 53-54,
  `(time / 500) % n`. Each icon is the frame drawn at (4,4) on a 16x16 canvas, `GlowRedrawer.outlineGlow` (gradient SUBTRACT
  0..0x28 over the lower half, black outline alpha 0.8 blur 1.4 strength 255), then `GlowFilter(0, 0.3, 6, 6, 2, LOW)`. Icons sit
  contiguously, 16 px each, centered on the sprite, top edge at the sprite's top - 8; drawn after the sprite, before name and
  health bar.
- **Names** (`drawName`): `SimpleText` 16 pt bold white, `GlowFilter(0,1,3,3,2,1)` on a `width x 64` bitmap, left edge at
  `posS[0] - (width/2 + 1)`, top at the ground position. Drawn for other players always (`Player.draw`) and for objects with
  `ShowName` whose name stat is non-empty; never for the local player. After the icons, before the health bar.
- **Health bar** (`drawHpBar`): unchanged (task 04 era); 40x5, back 0x111111, fill 0x10FF00 / 0xFF8010 below 50% / 0xE01010 below
  20%; other players are 16 px lower. **Breath bar** (`Player.drawBreathBar`, local player only while the breath stat is >= 0): 40x4
  at +9 below the ground position, back 0x545454 lerped toward 0xFF0000 by `|sin(t/300)| * (20-breath)/20` once `breath <= 20`,
  fill 0x26CAFF of width `breath/100 * 40`. Stat boosts are text only (`StatView`/`StatusBar` already done): green 0x5EB531 when
  boosted, gold 0xFCDF00 at the cap, red 0xFF3838 negative; there is no boost segment in the bars.
- **Floating text** (`CharacterStatusText`, `MapOverlay.addStatusText`): `SimpleText` 24 pt bold, `GlowFilter(0,1,4,4,2,1)`, centered
  on `(posS[0], posS[1] - texture.height * size/100 * 5 - 20 - drift)`, `drift = dt / lifetime * 40`; starts at the first draw plus
  its offset, removed when expired or the object leaves the map. Drawn above the world in `MapOverlay`.
  - `GameObject.damage`: for each DAMAGE effect (Quiet, Weak, Slowed, Sick, Dazed, Blind, Hallucinating, Drunk, Confused,
    Stun Immune, Invisible, Paralyzed, Speedy, Bleeding, Stasis, Stasis Immune, Armor Broken, and Stunned unless immune) the
    condition name in red for 3000 ms, 500 ms apart; a Stunned effect on a stun-immune target shows "Immune" (red, 3000 ms) with no
    offset. Then `-N` for 1000 ms, red 0xFF0000, purple 0x9000FF if the target is Armor Broken or the shot pierces armor.
  - `onNotification`: the packet's text and color (alpha byte ignored), 2000 ms, any object (`allyNotifs` defaults on).
  - `onNewTick`: for a Player seen before (level != -1): level up "Level Up!" green 2000 ms; otherwise exp gain "+N EXP" green
    1000 ms (not at level 20) and, if the character fame also rose at level 20, "+N Fame" 0xE25F00 1000 ms.
  - `QueuedStatusText` is wired but nothing dispatches it, so it is not reproduced.
- **Sprite effects** (`getTexture`): Stasis greyscales the sprite (`ColorMatrixFilter` 0.3/0.59/0.11, any object); Invisible
  players draw at 40% alpha (not while in Stasis); players under 20% health pulse `rv = int(|sin(t/200)| * 10) / 10`, added
  as `+128rv` red and `-128rv` green/blue; Hexed players take a random `Group Hexable` object's texture (dyes kept); while the
  focus player is Hallucinating every ordinary object draws a random `lofiChar8x8` tile (non-players scaled by
  `min(1.5, original width / 8)`). Healing particles and the Drunk/Blind map filters are from tasks 08 and 10.

Native: `conditions.hpp`, `status_effects.{hpp,cpp}` (labels, floating-text queue, bar colors), `Appearances::condition_icon`,
`World::notify` / `take_status_events`, the `Scene` draw lambda (icons, names, breath bar, sprite effects) and the overlay in `main.cpp`.

## Options screen (task 12)

Verified against `ui/options/{Options,Option,KeyMapper,KeyCodeBox,ChoiceOption,ChoiceBox,OptionsTabTitle}.as`,
`parameters/Parameters.as`, `util/KeyCodes.as`, `ui/layout/{ScaledScreen,MenuFrame}.as`, `ui/tooltip/{ToolTip,TextToolTip}.as`,
`ui/IconButton.as`, `kabam/rotmg/ui/view/{CharacterDetailsView,CharacterDetailsMediator,HUDView}.as` and `game/MapUserInput.as`.

- Screen (`ScaledScreen`, 800x600 design space scaled by height): backdrop 0x2B2B2B at alpha .8 over the whole window, a one pixel
  0x5E5E5E rule at y 100, MenuFrame (bar 0x545454 at y 524-576 plus the black glow band), title "Options" 36 bold at y 8 centered,
  tabs at x 14 + 108 i, y 70 (16 bold; 0xB3B3B3, hover white, selected 0xFFC800), "continue" 36 centered at y 520, "reset to
  defaults" 22 at (20, 532), "back to home" 22 at (620, 532); buttons tint 0xFFDC85 on hover.
- Grid: two columns at x 20 and 415, y = 122 + 44 * floor(index / 2); spacer Sprites occupy cells. Each option is an 80x32 box (fill
  0x333333, 2 px border 0x444444 / 0xB3B3B3 hover; KeyCodeBox a plain rectangle, ChoiceBox a cut-edge rect) and an 18 pt 0xB3B3B3
  label at x 104. Box text is 16 bold white centered; key boxes show `KeyCodes.CharCodeStrings` names, "[Unset]" for 0, and while
  waiting alternate "" / "[Hit Key]" every 400 ms. Disabled KeyMappers (Rotate Left/Right while Allow Camera Rotation is off) draw
  at 0.6 brightness and take no input. A mouse press anywhere ends waiting; the click that did it then acts normally.
- Tabs and rows (Options.as add order): Controls 14 cells, Gameplay 9, Hot Keys 16, Chat 6, Graphics 9, Sound 3 (see
  `tests/options_source_tests.py`, which compares labels, tooltips, choice lists, defaults, order and spacers with the source).
  ChoiceBox click advances to the next value, wrapping. Option tooltips are `TextToolTip(0x272727, 0x828282, 150)` on the label.
- `Parameters.setKey` unsets any other registered binding on the key (including `escapeToNexus2`, F5, which has no row).
  "reset to defaults" deletes the visible tab's parameters and runs `setDefaults`, so only that tab changes. Every change saves.
- ToolTip frame: cut-edge rect (-6, -6, w + 12, h + 12) cut 4, 1 px outline, DropShadow blur 16; TextToolTip fields keep their
  maxWidth (a wordWrap SimpleText) so the frame is maxWidth + 12 wide; height = title (26 for one 20 pt line) + 8 + tip
  (16.8 per line + 4). Placed beside the pointer: x + 12 (left half) or x - width - 1, y + 12 (top third) or y - height - 1, both >= 12.
- CharacterDetailsView: an `IconButton` at (172, 4) of the panel at (0, 198), its 40x40 bitmap at (-12, -12): lofiInterfaceBig frame 5
  "Options" when `gameId == NEXUS_GAMEID` (-1), frame 6 "Nexus" otherwise (click = escape). Hover applies ColorTransform(1,
  220/255, 133/255) and a TextToolTip(0x363636, 0x9B9B9B, title, "Hotkey: KEY", 200).
- Effects of the flags: `hpBars` (GameObject.draw), `drawShadows` (Map.draw), `particles` (Projectile hit effects and trails,
  GameObject.damage, Character.explode, Healing and ParticleEffect.fromProps; ShowEffect packets and AoE always play), `projOutline`
  (Projectile.draw), `textBubbles` (SpeechBalloon), `allyShots` (SERVERPLAYERSHOOT of others, ALLYSHOOT), `allyDamage` (DAMAGE to
  others), `allyNotifs` (NOTIFICATION to other players and their level-up / EXP text), `showTierTag` (item tiles), `centerOnPlayer`
  (Camera), `allowRotation` (rotate keys), `cursor` (cursorsEmbed 32x32 cells, hot spot 15,15), `vsync`/`maxFPS`/`fullscreenMode`
  (stage frame rate and display state), `showTradePopup` (no row). `quality` only affects UI vector quality in Flash.

Native: `settings.{hpp,cpp}` (table, persistence), `options.{hpp,cpp}` (screen), `text_tooltip.{hpp,cpp}`, `sdl_keys.hpp`
(SDL <-> Flash key codes), `ui_menu_backdrop`, `World::preferences`, `HudState::Header` and `main.cpp` (key dispatch, apply, save).
