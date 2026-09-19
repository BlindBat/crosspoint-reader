# Phase 0 Research: Stub Cover Sleep Screen

No `NEEDS CLARIFICATION` markers were carried in from the spec. What follows are the four
decisions that shape the implementation, each taken against what the tree already provides.

## 1. Wrapping engine: reuse `UITheme::drawCenteredWrappedText`

**Decision**: draw both texts with `UITheme::drawCenteredWrappedText()`
([src/components/UITheme.h:28-31](../../src/components/UITheme.h),
[src/components/UITheme.cpp:145-181](../../src/components/UITheme.cpp)), one call per text, each
given a bounds rectangle.

**Rationale**: it already does everything FR-009 to FR-012 ask for. It wraps only when the text
overflows (a short title still takes the single-line path), it clamps the line count to
`bounds.height / lineHeight` so text physically cannot reach the frame, it centres each line
horizontally within the bounds, and it aligns the finished block top, centre or bottom. It sits
behind the `UITheme` seam that AGENTS.md requires all UI rendering to go through, so using it is
more compliant than the raw `drawCenteredText` calls it replaces.

**Alternatives considered**:

- *Call `renderer.wrappedText()` and loop over the lines in `SleepActivity`* — the idiom used by
  `LyraTheme` ([src/components/themes/lyra/LyraTheme.cpp:274-285](../../src/components/themes/lyra/LyraTheme.cpp)).
  Rejected: it re-derives block height, start Y and the per-line step that the UITheme helper
  already encapsulates, for no gain.
- *A new wrapping helper in `SleepImageUtils`* — rejected outright: a second implementation of
  wrapping in a codebase that already has one, with its own bugs to find.

## 2. Vertical balance: two bounds meeting at a fixed divider

**Decision**: give the title a bounds rectangle ending at a fixed divider line and align it
`BOTTOM`; give the author a bounds rectangle starting at that divider and align it `TOP`.

**Rationale**: FR-012 wants the block balanced at any line count. With a fixed divider the title
grows upward and the author downward, so the pair stays centred on the divider automatically — no
block-height arithmetic in the activity, and the single-line case lands where it does today. The
helper's own line clamp then guarantees FR-011 and FR-012's no-overlap rule as a side effect: a
bounds rectangle that stops short of the frame cannot produce a line that crosses it.

**Alternatives considered**:

- *Keep the current fixed title Y and let the block grow downward* — simplest of all, but a
  three-line title plus a two-line author hangs visibly low on the card.
- *Compute total block height and centre it* — the LyraTheme approach; correct, but it is the
  arithmetic the divider trick avoids.

## 3. Overlong words are ellipsised, not hyphenated

**Decision**: accept `wrappedText`'s existing behaviour — a single word wider than the line is
truncated with an ellipsis ([lib/GfxRenderer/GfxRenderer.cpp:1840-1850](../../lib/GfxRenderer/GfxRenderer.cpp)).

**Rationale**: FR-011 requires that such a word not overflow the frame, and truncation satisfies
that. Hard-splitting a word mid-grapheme is worse typography than an ellipsis and would need
UTF-8 and combining-mark handling that the truncation path already owns. The reader's hyphenation
engine is a paragraph-layout facility and is not reachable, or appropriate, here.

**Alternatives considered**: hyphenated mid-word break (more code, worse result); shrink-to-fit
font size (breaks the card's two-size hierarchy and multiplies the metric queries).

## 4. No host test; verification is the simulator

**Decision**: add no host suite. Verify on the simulator per constitution gate 6, using the recipe
in [quickstart.md](quickstart.md).

**Rationale**: Principle V binds *host-reachable* logic. After decisions 1 and 2, the new code in
`SleepActivity` is render calls plus one three-way branch (`cover art` / `stub` / `previous
fall-back`) that reduces to a single ternary over two booleans, inside a class that pulls in the
display, settings and storage singletons and does not host-compile. The logic that could fail
silently — wrapping, ellipsis placement, allocation growth — is in `GfxRenderer::wrappedText`,
already covered by `test/gfx_renderer/GfxRendererTest.cpp` and `GfxRendererAllocTest.cpp`.

**Alternatives considered**: extracting the branch into `sleepimage::` to reach the existing
`test/platform_helpers` suite. Rejected: the extracted function would be one line, and a test
asserting a one-line ternary neither fails when the feature breaks nor documents anything the
spec does not. Principle V's own wording — a test only counts if it fails when the behaviour it
guards is broken — argues against it. This is recorded here rather than left implicit so a
reviewer can overrule it cheaply.

## Implementation shape

For `/speckit-tasks`. Both files are already open on this branch from the retrospective restore.

1. `renderCoverSleepScreen()` — capture `bookTitle`/`bookAuthor` from each format's book object
   while it is alive, let a failed cover generation fall through instead of returning early, and
   after the cover BMP attempt fails, call the card renderer when a title exists (**done on this
   branch**; it is the retrospective half of the spec).
2. `renderCoverStubSleepScreen()` — replace the two `truncatedText` + `drawCenteredText` pairs
   with two `UITheme::drawCenteredWrappedText` calls plus the two bounds rectangles from decision
   2 (**the enhancement; not yet done**).
3. Treat a whitespace-only title as absent (FR-005) — one condition at the call site.
4. Gates: `./bin/clang-format-fix -g`, `bin/run-tests`, `--asan`, `pio run -e default`,
   `pio check`, then the simulator recipe.
