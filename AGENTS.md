# C++ client priorities

Latest user instruction (2026-10-10, refines the paragraph below): HUD work
must look almost 1:1 with Flash, the same at a glance. Per-pixel exactness is
not required; do not reverse-engineer Flash rasterization solely to close
pixel diffs. Layout, sizes, art, colors, text content and visual states must
still match closely, verified by side-by-side review against source captures.

Earlier instruction (2026-10-10): this player tooltip and all future
elements must appear 1:1 with the original Flash client. This supersedes the
earlier visual approximation policy below. Preserve original layout, sizing,
fonts, art, colors, transparency, shadows, layering and visual states; validate
new or revised elements against source captures. Do not introduce visual
redesigns or accept appearance differences solely to simplify implementation.
Native performance, bounded memory, protocol correctness and maintainability
remain priorities within that visual contract. Record any remaining comparison
differences honestly; do not describe incomplete matching as 1:1.

The following records the earlier scope and remains applicable only where it
does not conflict with the latest instruction.

The user clarified the scope on 2026-10-10: Flash is the visual and functional
shell for a better native client. UI and playfield rendering should look
basically the same. Performance and improvements over Flash take priority
over reproducing its pixels or implementation quirks.

Read `docs/port-spec.md` for the current contract and `docs/progress.md` for
implementation evidence. Historical exact-match results describe past work;
they do not impose new completion requirements.

- Preserve server compatibility, authoritative game state, gameplay rules,
  familiar UI structure, original art, and readable combat information.
- Prioritize a complete usable client, responsiveness, stable frame times,
  bounded memory, and maintainable native implementations. Profile actual
  workloads to choose performance work.
- Use normal-size visual review to judge resemblance. Fix missing content,
  unreadable or materially clipped text, bad layout, incorrect layering,
  misleading combat cues, and distracting artifacts.
- Small font coverage, kerning, subpixel placement, antialiasing, shadow,
  color quantization, and minimap-arrow differences are acceptable. Do not
  spend work reverse-engineering Flash to eliminate these differences.
- Exact Flash image comparisons are optional diagnostics. Their nonzero
  diffs do not block progress or completion by themselves. Keep meaningful
  correctness, protocol, memory safety, cache, and rendering regression checks.
- Prefer efficient standard native text/rendering approaches when they meet
  the visual goal. Existing Flash baking and lookup machinery may be reused,
  simplified, or replaced as useful; do not extend it solely for pixel parity.
- Routine visual and usability improvements within the familiar design are
  authorized. Broad redesigns and changes to gameplay rules need user direction.
- Runtime remains native. Measure and describe performance honestly; isolated
  renderer fixtures do not prove a whole-client speedup.
