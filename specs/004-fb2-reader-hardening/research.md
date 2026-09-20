# Phase 0 Research: FB2 Reader Hardening

All findings are from the code in this repository at `feature/fb2-reader-hardening`, cited by
file and line. No API is assumed that was not read.

## Decision 1 — Chapter ceiling: one value, 256, not board-dependent

**Decision**: `Fb2::FB2_MAX_CHAPTERS` becomes **256**, a single value on every board.

**Rationale (mechanism, not assertion)**: chapter metadata is a
`std::vector<Fb2::SectionInfo>` ([lib/Fb2/Fb2.h:19-24](../../lib/Fb2/Fb2.h)). One entry on a
32-bit target is `std::string` (24 B: pointer + size + 16 B SSO buffer) + two `size_t` (8 B) +
`level` (1 B), padded to 36 B, **plus** one heap block per title longer than the 15-char SSO
limit. At 1024 the vector alone is ~37 KB and titles add a block each; the host allocator
measured the whole of `Fb2::load()` at **148,328 bytes** at the cap
([test/fb2_book/Fb2BookTest.cpp:340-382](../../test/fb2_book/Fb2BookTest.cpp)). At 256 the same
structure costs a quarter of that — ~9.2 KB of vector plus ≤256 title blocks, bounded to
~36 KB worst case by Decision 4's title cap. That is affordable beside the 48 KB framebuffer
on the C3; 148 KB is not.

**Why 256 and not lower**: the reference anthology is 66 chapters. 256 is ~4× the largest book
anyone has reported, so FR-003's degradation (sections past the ceiling read as part of their
parent) stays theoretical for real books while the worst case drops by 111 KB.

**Why not board-dependent** (FR-002 permits it; we decline): two ceilings mean the same SD card
carries caches one board accepts and the other rejects. The rejection path exists and is
correct ([lib/Fb2/Fb2.cpp:96](../../lib/Fb2/Fb2.cpp) already rejects
`sectionCount > FB2_MAX_CHAPTERS`), so nothing breaks — but it buys a silent reparse on every
card swap to serve a book that does not exist. YAGNI; a single constant is also what makes
FR-007's "one place in the code" true. If a real book ever exceeds 256, issue #8's SD-resident
LUT is the answer, not a second constant.

**Alternatives considered**: 512 (halves the saving for no known benefit); board-gated
256/1024 (above); keeping 1024 and relying on the OOM path (the device aborts, per
`-fno-exceptions`).

**Free consequence**: no new invalidation logic is needed for caches built at the old ceiling —
`loadMetadataCache` already rejects a count above the cap and reparses.

## Decision 2 — Chapter list: copy EPUB's window, skip its prewarm

**Decision**: replace `rowLabels` / `rowItems`
([src/activities/reader/Fb2ReaderChapterSelectionActivity.h:18-19](../../src/activities/reader/Fb2ReaderChapterSelectionActivity.h))
with the fixed 24-row window EPUB already uses
([src/activities/reader/EpubReaderChapterSelectionActivity.h:21-26](../../src/activities/reader/EpubReaderChapterSelectionActivity.h)),
refreshed from `nav.top` in `buildScreen()` and handed to `screen.list()` with
`props.itemsWindowFirst`.

**Rationale**: `fui::ListProps` already supports a windowed item array with an absolute base
index — EPUB proves the mechanism, so this is reuse, not new machinery. Memory becomes
`24 × (std::string + ListItem)` regardless of chapter count, satisfying FR-005 and SC-003.

**Skipped deliberately**: EPUB's `prewarmFallbackText` batch after each window refresh
([EpubReaderChapterSelectionActivity.cpp:84-94](../../src/activities/reader/EpubReaderChapterSelectionActivity.cpp)).
It exists because EPUB TOC entries are SD LUT reads and its lists reach 547 entries; FB2
entries are already in RAM and the list works today without it. Add it if CJK FB2 lists repaint
slowly on device.

**Also skipped**: EPUB's `fcm->clearCache()` on entry, for the same reason — it pays for an
SD-backed list's glyph pressure that FB2 does not have.

**Alternatives considered**: keeping the full list and only dropping the second label copy
(still O(chapters)); windowing at a different size (24 matches EPUB and the screen's row count).

## Decision 3 — Derived labels are produced by the metadata parser, not at display time

**Decision**: when a `<section>` closes with an empty title, `Fb2MetadataParser` stores a
bounded prefix of that section's own first `<p>` as its label and marks it derived. The chapter
list renders derived labels through a translated format string so they are distinguishable
(FR-016).

**Rationale**: expat is what makes the text correct. FB2 files are frequently windows-1251 or
koi8-r; the parser registers those encodings
([lib/Fb2/Fb2/Fb2XmlEncoding.cpp](../../lib/Fb2/Fb2/Fb2XmlEncoding.cpp), used at
[Fb2MetadataParser.cpp:224](../../lib/Fb2/Fb2/Fb2MetadataParser.cpp)) and hands handlers UTF-8.
Deriving a label at list-build time instead would mean reading raw bytes at
`SectionInfo.fileOffset`, stripping tags by hand, and transcoding — mojibake waiting to happen,
plus an SD read per visible row on every window scroll. The parser already buffers text for
`<title>` (`charBuffer`, `Context::SECTION_TITLE_P`), so this is one more context on an
existing state machine.

**Storage**: the derived text goes in the existing `SectionInfo.title` field — it is empty by
definition in this case — so the chapter list, `getTocEntry` and every consumer work unchanged.
A `flags` byte carries "this title was derived".

**Alternatives considered**: display-time derivation (above); a separate `derivedLabel` string
per chapter (doubles the per-chapter cost this feature is trying to cut); encoding the derived
marker inside the string (a format change in disguise, and it leaks into anything that prints a
title).

## Decision 4 — One title cap, shared by real titles and derived labels

**Decision**: add `Fb2::FB2_MAX_TITLE_CHARS = 64`, applied with
`utf8TruncateChars` ([lib/Utf8/Utf8.h:13](../../lib/Utf8/Utf8.h)) to both a parsed `<title>` and
a derived label before either is stored.

**Rationale**: FR-018 requires derived labels bounded and cut on character boundaries; the repo
already has the UTF-8-safe truncator, so this is rung 2 (reuse), not new code. Applying the same
cap to real titles is what makes the ceiling's memory bound tight: without it a chapter title is
bounded only by `FB2_CACHE_MAX_STRING` (4096,
[lib/Fb2/Fb2.cpp:21](../../lib/Fb2/Fb2.cpp)), i.e. 256 × 4 KB = 1 MB in the worst case. 64
characters is more than any chapter list row can display at any orientation.

**Alternatives considered**: capping only derived labels (leaves the real-title worst case);
capping in bytes (cuts multi-byte characters in half — the reference book is Russian).

## Decision 5 — `book.bin` v4, and what it costs

**Decision**: bump `FB2_CACHE_VERSION` 3 → 4 and append a `flags` byte to each chapter record.

**Rationale**: the derived-label marker has to survive the cache round trip, or a book shows
different labels before and after its second open. The cost is bounded and self-healing:
`Fb2::load()` falls back to `parseMetadata()` on a version mismatch
([lib/Fb2/Fb2.cpp:186-206](../../lib/Fb2/Fb2.cpp)) — **one expat metadata pass per FB2 book, on
first open after the update**. It does *not* touch `sections/<n>.bin`, which are keyed by
`FB2_SECTION_FILE_VERSION` and the render spec, so no book is re-paginated and the ~32 s
first-section rebuild quoted in issue #4 does not apply here.

**Spec deviation, recorded**: SC-006 says no FB2 cache written before this feature is
invalidated by it. This bump invalidates `book.bin` for every FB2 book. FR-012 — the requirement
that actually protects the reader — is about the *cover page*, and it holds: the cover
contributes no format change. SC-006 is therefore narrowed in Complexity Tracking to "no page
cache is invalidated and no reading position is lost", which is what the reader experiences.
The alternative (no bump) means every book already on the device keeps showing `Unnamed` until
something else rebuilds its metadata — FR-015 satisfied only for new books, which is worse.

**Alternatives considered**: no bump (above); a sidecar file for the flags (a second file to
open, validate and keep in sync, to carry one bit per chapter).

## Decision 6 — The cover page is reader state, not a page

**Decision**: `Fb2ReaderActivity` gains `bool onCoverPage`. It is set at load time when the
restored position is chapter 0 / page 0 and the book has a cover. `renderBook()` loads the
section as usual, then draws the cover instead of the page while the flag is set. A forward turn
clears it; a back turn from chapter 0 / page 0 sets it.

**Rationale**: this is what satisfies FR-011 and FR-012 at once. No chapter index moves, no page
number moves, `progress.bin` keeps its meaning, and `sections/<n>.bin` is untouched — the cover
is not in the pagination at all. It mirrors the sentinel the reader already uses at the other
end of the book (`currentSectionIndex = getSectionCount()` for end-of-book,
[Fb2ReaderActivity.cpp](../../src/activities/reader/Fb2ReaderActivity.cpp) `pageTurn`), so the
shape is familiar.

**Why load the section anyway**: `bookProgressPercent()` and the reader menu read
`section->pageCount` ([Fb2ReaderActivity.cpp:83-89, 104-107](../../src/activities/reader/Fb2ReaderActivity.cpp)).
Drawing the cover before the section exists would show "0 / 0" in the menu. Loading first costs
nothing overall — the page after the cover needs it.

**Drawing**: reuse `sleepimage::calculateBitmapPlacement(w, h, pageW, pageH, crop=false)`
([src/activities/boot_sleep/SleepImageUtils.h:38](../../src/activities/boot_sleep/SleepImageUtils.h)),
`Bitmap` + `renderer.drawBitmap(...)`, exactly as `SleepActivity::renderBitmapSleepScreen` does,
minus the grayscale three-pass and the sleep-screen invert filter. `Fb2::generateCoverBmp()`
short-circuits when `cover.bmp` already exists
([lib/Fb2/Fb2.cpp](../../lib/Fb2/Fb2.cpp) `generateCoverBmp`), so calling it once at book load
is free for a book whose cover was already extracted for the home or sleep screen.

**Skipped deliberately**: the grayscale cover pipeline. It is three extra full-buffer passes and
the reader's refresh policy is not the sleep screen's. Add it if a dithered cover looks poor on
device.

**Alternatives considered**: making the cover page index −1 in the section numbering (touches
every clamp and the progress math); giving the cover a real page inside chapter 0 (shifts every
saved page number and forces a section-cache bump — exactly what FR-011/FR-012 forbid).

## Non-decisions (confirmed unchanged)

- `Fb2SectionParser`'s `[Image]` placeholder
  ([Fb2SectionParser.cpp:219](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)) stays as it is — FR-014.
- Chapter numbering, `calculateProgress`, and contract rules C1–C10 from
  `003-fb2-nested-chapters` are untouched by every decision above.
- `FB2_SECTION_FILE_VERSION` is not bumped.
