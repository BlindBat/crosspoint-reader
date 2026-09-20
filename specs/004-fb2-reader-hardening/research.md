# Phase 0 Research: FB2 Reader Hardening

All findings are from the code in this repository at `feature/fb2-reader-hardening`, cited by
file and line. No API is assumed that was not read.

## Measurement basis

Three things were measured before any ceiling was chosen. All are reproducible.

**M1 — Device budget** (`pio run -e default`, this tree):

```text
RAM:   [==        ]  17.2% (used 56348 bytes from 327680 bytes)
```

327,680 B DRAM − 56,348 B static = **271,332 B free at boot**; minus the 48 KB framebuffer =
**222,180 B** of heap for everything the reader does (section pagination, page cache, font
decompression, activities).

**M2 — Device ABI** (`riscv32-esp-elf-g++ -std=gnu++2a`, sizes read out of the object file's
`.srodata`, not guessed): `sizeof(std::string)` = **24** (SSO capacity 15 chars),
`sizeof(Fb2::SectionInfo)` = **36**, and adding a second `uint8_t` beside `level` keeps it at
**36** — the derived-label flag is free in existing padding.

Device cost model for a book's chapter metadata, used throughout:

```text
bytes = chapters × 36  +  Σ over titles ( len > 15 ? align4(len+1) + 8 : 0 )
```

(8 B = ESP-IDF heap block header; the SSO threshold is M2's measured 15.)

**M3 — Real corpus**: **2,899 real FB2 books** (2.7 GB, a Calibre library) parsed through this
repo's own `Fb2MetadataParser`, not by counting `<section` with grep — the parser skips
`<body name="notes">`, and grep overstates chapter counts by ~40%.

| | chapters |
|---|---|
| median | 18 |
| mean | 29.4 |
| p90 | 64 |
| p99 | 200 |
| max | 1024 — **2 books sit exactly at today's cap**, i.e. the cap already degrades real books |

Device metadata cost across the corpus: median **988 B**, p99 **19.4 KB**, worst **99,716 B**.

## Decision 1 — Chapter ceiling: 256, chosen from the corpus, not from a round number

**Decision**: `Fb2::FB2_MAX_CHAPTERS` becomes **256**, a single value on every board.

**The trade, measured**. "Degraded" means chapters past the ceiling stop being boundaries and
read as part of the chapter containing them (contract C6) — no text is lost, but the tail
collapses into one very large chapter.

| Ceiling | Worst real book's metadata | Share of the 222,180 B usable heap | Books degraded (of 2,899) | Median share of a degraded book's text in the tail chapter |
|---|---|---|---|---|
| 1024 (today) | 99,716 B | **44.9 %** | 0 (2 sit at the cap) | — |
| 512 | 89,908 B | 40.5 % | 9 (0.31 %) | 10.5 % |
| 384 | 63,240 B | 28.5 % | 12 (0.41 %) | 27.4 % |
| **256** | **43,732 B** | **19.7 %** | **22 (0.76 %)** | **31.5 %** |
| 128 | 21,516 B | 9.7 % | 70 (2.41 %) | 33.3 % |

**Rationale**: 1024 is not a ceiling the device can afford — one real book in this corpus costs
**45 % of the heap the reader has left after the framebuffer**, before a single page is
paginated. 256 more than halves that to 19.7 % and costs coarser navigation in 0.76 % of books,
all of them reference works (encyclopaedias, collected letters, legal codes), none of them
novels. Every one of those 22 books still opens and still contains all of its text.

**What the measurement also shows, and what it costs us**: a count cap is a *weak proxy* for the
resource it is protecting. At 256 chapters, real books span roughly 9 KB to 44 KB of metadata —
a 5× spread — because the cost is dominated by title lengths, not chapter count. And the
degradation is not free: at 256, the worst affected book (a 1.79 MB encyclopaedia) puts **77 %
of its text into a single chapter**, which is a pagination cost, not just coarser navigation.
Both are arguments for issue #8's SD-resident LUT, which removes the ceiling rather than tuning
it. 256 is the best available answer while the metadata stays in RAM; it is not a good answer in
absolute terms.

**The alternative the data suggests, and why it is not taken here**: cap the *bytes*, not the
count — stop creating boundaries once chapter metadata reaches a fixed budget (say 24 KB). That
bounds the actual resource and adapts to title length, where a count cannot. It costs one
running accumulator in each parser, but it needs a count bound anyway for cache validation
(`u16 chapterCount`, `countFitsRemainingFile`), so it adds a second constant rather than
replacing one. Recorded as the upgrade path if #8 is deferred again.

**Why not board-dependent** (FR-002 permits it; we decline): two ceilings mean the same SD card
carries caches one board accepts and the other rejects. The rejection path already exists
([lib/Fb2/Fb2.cpp:96](../../lib/Fb2/Fb2.cpp) rejects `sectionCount > FB2_MAX_CHAPTERS`), so
nothing breaks — but it buys a reparse on every card swap to serve 22 books out of 2,899. A
single constant is also what makes FR-007's "one place in the code" true.

**Alternatives considered**: 512 (40.5 % of heap is still most of the budget, for 13 fewer
degraded books); 128 (9.7 %, but triples the degraded set to 70 books); keeping 1024 (the
measured hazard above).

**Free consequences**: caches built at the old ceiling are rejected and reparsed by the existing
count check — no new code. And on the parse path `sections` grows by doubling, so peak metadata
during a parse is up to ~1.5× the steady-state figure (new block + old block before the copy is
freed); one `shrink_to_fit()` after the parse returns the slack.

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

## Decision 4 — Cap derived labels only; do **not** cap real titles

**Decision**: add `Fb2::FB2_MAX_LABEL_CHARS = 64`, applied with `utf8TruncateChars`
([lib/Utf8/Utf8.h:13](../../lib/Utf8/Utf8.h)) to a **derived** label only. A `<title>` the book
actually supplies is stored as-is, unchanged from today.

**Rationale**: the draft of this plan capped every stored title, on the assumption that titles
were a meaningful part of the ceiling's memory. The corpus says they are not. Applying a 64-char
cap to every title in the worst real book moves its metadata from 99,716 B to 96,668 B — **a
3 % saving** — because only 13.6 % of chapters across 85,128 real chapters have a title longer
than 64 characters, and the per-chapter 36 B struct dominates. Capping real titles would change
what the reader sees in 34 % of books to save 3 % of a figure the ceiling already bounds. Not
worth it.

A derived label is different in kind: it is prose, and a `<p>` can be a whole paragraph, so
without a cap one untitled section could store kilobytes. The cap is there to bound the
*unbounded* case, which is the only case that needs it. It is also what FR-018 asks for.

`utf8TruncateChars` is reused rather than written: FR-018 requires truncation on character
boundaries and the repo already has the UTF-8-safe truncator.

**Alternatives considered**: capping all titles (measured above — 3 % for a visible behaviour
change); capping in bytes (cuts multi-byte characters in half; the corpus is overwhelmingly
Russian); no cap at all (leaves a paragraph-sized label in RAM and in `book.bin`).

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
