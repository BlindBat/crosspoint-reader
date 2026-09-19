# Phase 1 Data Model: Stub Cover Sleep Screen

This feature stores nothing. There is no new file, no new field in an existing file, no cache and
therefore no cache version to increment. What follows is the transient state the card is drawn
from, recorded so `/speckit-tasks` and any later reader can see the full extent of it.

## Entities

### Book metadata (read-only, borrowed)

| Field | Source | Lifetime | Notes |
|---|---|---|---|
| `title` | `Epub::getTitle()`, `Fb2::getTitle()`, `Txt::getTitle()`, `Xtc::getTitle()` | copied out of the book object before it leaves scope | The only mandatory input; an absent title suppresses the card entirely (FR-005). |
| `author` | `Epub::getAuthor()`, `Fb2::getAuthor()`, `Xtc::getAuthor()` | as above | Optional. Plain text and Markdown expose no author, so their cards are title-only. |

The copies exist because the format object is a local inside the per-format branch of
`renderCoverSleepScreen()` and is destroyed before the card is drawn. Two short strings, at sleep
entry, freed at function exit.

**Validation**: both strings are untrusted container content (Principle VI). They are passed only
to text APIs that take a length-bounded `const char*` from a `std::string`, never to a C API as a
`string_view`, and never used to size an allocation. Length is bounded at render time by the wrap
helper's line clamp, not by pre-validation.

### Cover availability (derived, transient)

| Value | Meaning |
|---|---|
| cover path non-empty **and** the BMP header parses | Real cover art — the card is not drawn. |
| cover path empty (generation failed or format has none) | No cover art. |
| cover path non-empty but the file is missing or the header rejects it | No cover art. |

Derived inside `renderCoverSleepScreen()` and discarded. Nothing is remembered between sleeps, so
a book whose cover art appears later needs no invalidation.

### Sleep presentation settings (read-only, existing)

| Setting | Use here |
|---|---|
| `sleepScreen` | Selects Cover or Cover + Custom, and which fall-back applies when no card can be drawn. Unchanged by this feature. |
| `sleepScreenCoverFilter` | `INVERTED_BLACK_AND_WHITE` inverts the finished card (FR-013). `BLACK_AND_WHITE` has no visible effect on a card that is already pure black and white. |
| `sleepScreenCoverMode` | Fit/Crop applies to bitmap cover art only; it has no meaning for a generated card and is not consulted. |

### Stub cover card (output, never persisted)

Drawn directly into the single framebuffer, then pushed with one half refresh. Two nested
rectangles inset from the screen edges, a bold title block above a fixed divider, a smaller author
block below it. No grayscale planes are involved, so no framebuffer snapshot is taken and the
`storeBwBuffer()`/`restoreBwBuffer()` pair that bitmap covers need is not on this path.

## State transitions

None. The card is a pure function of (title, author, filter, screen size) evaluated once per
sleep.
