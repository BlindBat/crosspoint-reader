# Feature Specification: FB2 Reader Hardening

**Feature Branch**: `feature/fb2-reader-hardening`

**Created**: 2026-09-20

**Status**: Draft

**Input**: User description: "Analyse all the open issues from the github (https://github.com/BlindBat/crosspoint-reader/issues) and specify them as a new feature"

## Context

Seven issues are open on the fork. Six were raised during hardware verification of
`003-fb2-nested-chapters`, which multiplied the chapter count of a real book from 4 to 66
and exposed costs that were negligible when FB2 books had only top-level sections.

This feature covers the three that make FB2 reading worse today, plus the memory ceiling
that makes a large FB2 unsafe to open at all. The other issues are dispositioned in
**Out of Scope** with the reason each is not being built now.

| Issue | Title | Disposition |
|---|---|---|
| [#6](https://github.com/BlindBat/crosspoint-reader/issues/6) | `FB2_MAX_CHAPTERS = 1024` costs 148 KB — too high for the C3 | **In scope** — User Story 1 |
| [#7](https://github.com/BlindBat/crosspoint-reader/issues/7) | FB2 chapter list materializes every row (EPUB windows to 24) | **In scope** — User Story 1 |
| [#5](https://github.com/BlindBat/crosspoint-reader/issues/5) | Render the FB2 cover as the book's first page | **In scope** — User Story 2 |
| [#10](https://github.com/BlindBat/crosspoint-reader/issues/10) | Untitled FB2 sections clutter the chapter list with "Unnamed" rows | **In scope** — User Story 3 |
| [#8](https://github.com/BlindBat/crosspoint-reader/issues/8) | Move FB2 chapter metadata to SD with a seekable LUT | Out of scope — documented upgrade path |
| [#4](https://github.com/BlindBat/crosspoint-reader/issues/4) | Prefetch the next chapter's page cache in the background | Out of scope — needs its own spec |
| [#9](https://github.com/BlindBat/crosspoint-reader/issues/9) | FB2 body-level front matter carries no progress weight | Out of scope — accepted limitation |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A large FB2 opens instead of exhausting the device (Priority: P1)

A reader copies a big anthology — hundreds of stories, deeply nested `<section>` elements —
onto the SD card and opens it. The book opens, its chapter list scrolls, and the device does
not run out of memory, regardless of how many sections the file contains. The reader is never
shown a chapter list that the device cannot afford to build.

**Why this priority**: this is the only item in the feature that can cost a reader their book,
and it is not hypothetical. Measured over **2,899 real FB2 books**, two of them already sit at
today's cap of 1024 chapters, and the worst costs **99,716 bytes** of chapter metadata on the
ESP32-C3 — **44.9% of the 222,180 bytes of heap the reader has left** once the measured 56,348
bytes of static allocation and the 48 KB framebuffer are accounted for, before a single page is
paginated. The cap bounds growth but is not a value the device can survive reaching.

**Independent Test**: build a synthetic FB2 whose section count exceeds the ceiling, open it
on the most constrained target, and confirm the book opens, every byte of text is still
reachable, the chapter list scrolls end to end, and total chapter-metadata allocation stays
inside the stated budget. Delivers value with neither of the other stories built.

**Acceptance Scenarios**:

1. **Given** an FB2 whose `<section>` count exceeds the chapter ceiling, **When** the reader
   opens it, **Then** the book opens, no text is lost (sections past the ceiling read as part
   of their containing chapter), and chapter-metadata memory does not grow with the file's
   section count beyond the ceiling.
2. **Given** an FB2 at the chapter ceiling, **When** the reader opens the chapter list,
   **Then** the list scrolls from first row to last and the memory it holds does not grow with
   the chapter count.
3. **Given** an SD card moved between boards, **When** the same book is opened on each,
   **Then** it is split into the same chapters on both, and a cache written on one board is
   either read correctly on the other or rejected and rebuilt — never misread.
4. **Given** the reference anthology (1.9 MB, 66 chapters, ~15 KB of chapter metadata),
   **When** it is opened after this change, **Then** its chapter list, navigation and progress
   are unchanged from today.

---

### User Story 2 - An FB2 book opens on its cover (Priority: P2)

A reader opens an FB2 book for the first time and sees its cover art, the same image the home
screen and the sleep screen already show for that book, before the first page of text.

**Why this priority**: the asset already exists — the cover is extracted to `cover.bmp` at
import and rendered by the sleep screen — so the reading flow is the only place it is missing.
Inside the book every FB2 image currently renders as the literal text `[Image]`
([lib/Fb2/Fb2/Fb2SectionParser.cpp:219](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)), which reads
as a defect rather than as an omission. Valuable, but nobody loses a book over it.

**Independent Test**: open an FB2 that has a cover, confirm the cover is displayed as the
book's first page, page forward into the text, then reopen the book at a saved position and
confirm the reader lands on exactly the text it left.

**Acceptance Scenarios**:

1. **Given** an FB2 with an extractable cover, **When** the reader opens it at the beginning,
   **Then** the cover is shown as the book's first page, scaled to the screen the way the
   sleep screen already scales it.
2. **Given** the cover page is displayed, **When** the reader turns forward, **Then** the first
   page of the first chapter follows; **When** the reader turns back from the first page of
   text, **Then** the cover is shown again.
3. **Given** an FB2 with no cover, or one whose cover cannot be decoded, **When** it is opened,
   **Then** the reader opens on the first page of text with no blank page and no error.
4. **Given** a book with a saved reading position from before this change, **When** it is
   reopened, **Then** the reader lands on the same text as before: chapter indices are
   unchanged and no saved position is shifted by the cover.
5. **Given** a book is open past its first page, **When** the progress percentage is displayed,
   **Then** it is unchanged by the existence of the cover page.

---

### User Story 3 - The chapter list reads as titles, not "Unnamed" (Priority: P3)

A reader opens the chapter list of a nested anthology and sees a recognisable label on every
row. Sections that carry no `<title>` of their own are labelled from their own first line of
text instead of showing a placeholder.

**Why this priority**: purely cosmetic — navigation is correct today. But an anthology's list
currently mixes real titles with several `Unnamed` rows, and the count grows with nesting
depth, which makes the list harder to scan.

**Independent Test**: open the chapter list of an FB2 containing untitled sections and confirm
each such row shows a truncated first line of that section's own text, that selecting it opens
that same section, and that a section with neither title nor text still shows the localized
placeholder.

**Acceptance Scenarios**:

1. **Given** a section with no `<title>` of its own but with text, **When** the chapter list is
   shown, **Then** its row is labelled with a truncated first line of that section's own direct
   text, marked as a derived label so it is distinguishable from a real title.
2. **Given** a section with neither a title nor direct text of its own, **When** the chapter
   list is shown, **Then** its row shows the localized placeholder as it does today.
3. **Given** any row in the list, **When** the reader selects it, **Then** the reader opens the
   same chapter it would have opened before this change — labels change, ordering and indices
   do not.
4. **Given** a derived label longer than the row can show, **When** it is rendered, **Then** it
   is truncated at a character boundary valid for the book's encoding, never mid-character.

---

### Edge Cases

- **A book above the ceiling**: sections past the ceiling stop creating chapter boundaries and
  read as part of their containing chapter (the behaviour established by contract C6 in
  `003-fb2-nested-chapters`). No text is lost and no book is refused.
- **A cache written at a different ceiling**: a `book.bin` whose chapter count exceeds the
  ceiling the running firmware enforces is rejected and rebuilt, not truncated and not trusted.
  This is the existing corrupt-cache path.
- **The same SD card moved between boards**: if the ceiling is board-dependent, a cache built
  on the more generous board must be rejected-and-rebuilt on the constrained one rather than
  partially read.
- **A cover that is not a valid image**: a `cover.bmp` that is missing, truncated, or lies
  about its dimensions must degrade to "no cover page", never to a crash or a blank page.
- **A cover larger than the screen**: scaled to fit, as the sleep screen already does
  (observed: a 498 × 800 bitmap on a 480 × 800 screen).
- **Turning back from the cover**: the cover is the first position in the book; a back turn
  from it does nothing (or leaves the reader, per the reader's existing first-page behaviour).
- **A derived label from non-Latin text**: labels are derived on character boundaries of the
  book's declared encoding; the reference anthology is Russian.
- **A section whose first text is an epigraph or an empty line**: leading whitespace is skipped
  when deriving a label; if nothing printable is found, the placeholder is used.
- **A book at the ceiling in the chapter list**: the list holds a bounded number of rows
  regardless of chapter count, so scrolling to the last chapter of the largest book allocates
  no more than scrolling the first.

## Requirements *(mandatory)*

### Functional Requirements

**Bounded chapter memory (#6, #7)**

- **FR-001**: The FB2 chapter ceiling MUST be a value the most constrained supported target can
  reach without exhausting memory: opening a book at the ceiling MUST leave the device able to
  render pages and show the chapter list. The ceiling MUST be justified against measured device
  figures — available heap and per-chapter cost — not against a round number.
- **FR-002**: The ceiling MUST be a single value shared by every board. A board-dependent
  ceiling was considered and rejected: it makes the same SD card carry caches one board accepts
  and another rejects, to serve 22 books in a 2,899-book corpus.
- **FR-003**: A book whose section count exceeds the ceiling MUST still open and MUST lose no
  text; sections past the ceiling read as part of their containing chapter.
- **FR-004**: Chapter-metadata memory MUST NOT grow with a file's section count once the
  ceiling is reached.
- **FR-005**: The chapter list MUST hold a bounded number of rows independent of the book's
  chapter count, matching the windowing the EPUB chapter list already uses
  ([src/activities/reader/EpubReaderChapterSelectionActivity.h:21-23](../../src/activities/reader/EpubReaderChapterSelectionActivity.h)),
  while scrolling and selection behave exactly as they do today.
- **FR-006**: A cached `book.bin` whose recorded chapter count exceeds the ceiling enforced by
  the running firmware MUST be rejected and the book re-parsed — never truncated, never
  partially trusted.
- **FR-007**: The ceiling's value MUST be stated in exactly one place in the code and mirrored
  in the live format documentation (`docs/file-formats.md`), so the documented range and the
  enforced range cannot diverge. A landed feature's specification is a record of what was decided
  then and MUST NOT be rewritten to a later value; `specs/003-fb2-nested-chapters` gets a forward
  pointer to this feature instead.

**Cover as first page (#5)**

- **FR-008**: An FB2 book that has an extractable cover MUST display that cover as its first
  page, before the first page of the first chapter.
- **FR-009**: The cover page MUST be reachable by turning back from the first page of text and
  MUST turn forward into it.
- **FR-010**: A book with no cover, or with a cover that cannot be read or decoded, MUST open
  on its first page of text with no blank page, no error screen, and no log-only failure the
  reader can see.
- **FR-011**: The cover page MUST NOT change chapter indices, MUST NOT shift any stored reading
  position, and MUST NOT alter the progress percentage of any position in the book.
- **FR-012**: The cover page MUST NOT invalidate existing FB2 caches: no cache version bump may
  be required by this requirement alone.
- **FR-013**: The cover MUST be scaled so the whole image is visible — centred and letterboxed,
  never cropped — at any orientation. (The sleep screen's crop is a user setting for the sleep
  screen; the reader does not inherit it.)
- **FR-014**: Inline FB2 images other than the cover remain unrendered; their current textual
  placeholder behaviour is unchanged by this feature.

**Chapter labels (#10)**

- **FR-015**: A chapter with no `<title>` of its own MUST be labelled in the chapter list with a
  truncated first line of its own direct text.
- **FR-016**: A derived label MUST be visually distinguishable from a real title so the reader
  can tell the book supplied no title.
- **FR-017**: A chapter with neither a title nor direct text of its own MUST keep the localized
  placeholder (`STR_UNNAMED`) it shows today.
- **FR-018**: Derived labels MUST be truncated at valid character boundaries for the book's
  encoding and MUST be bounded by a fixed character cap. Real titles the book supplies are
  stored as-is: measured, capping them too saves 3% of the worst book's metadata, which does not
  justify changing what the reader sees in a third of books.
- **FR-019**: Deriving a label MUST NOT change chapter ordering, chapter indices, navigation
  targets, or the progress percentage.
- **FR-020**: All user-facing text introduced or changed by this feature MUST go through the
  translation layer; no label or placeholder may be hardcoded.

**Cross-cutting**

- **FR-021**: Every byte this feature reads from SD — the chapter cache, `cover.bmp`, derived
  label text — MUST be validated against the physical file size and a sane cap before any
  allocation it drives, and malformed input MUST produce a bounded, deterministic failure.
- **FR-022**: The reference anthology's behaviour (66 chapters, existing navigation, existing
  progress) MUST be unchanged except for the cover page and the labels of its untitled rows.

### Key Entities

- **Chapter ceiling**: the maximum number of chapters an FB2 book may be split into. Bounds
  chapter-metadata memory; may vary per board; validated on cache load; mirrored in the format
  documentation.
- **Chapter metadata entry**: per chapter — title (or derived label), byte offset, byte length,
  nesting level. Held in memory for the open book; its per-book total is what the ceiling bounds.
- **Chapter list window**: the bounded set of chapter rows the selection screen holds at once,
  independent of the book's chapter count.
- **Cover asset**: the bitmap already extracted at import and already rendered by the home and
  sleep screens; consumed, not produced, by this feature.
- **Derived label**: a chapter-list label computed from a chapter's own first line of text when
  the book supplies no title. Display-only — never stored as if it were a title, never used for
  navigation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An FB2 whose section count exceeds the ceiling opens, is fully readable end to
  end, and its chapter list scrolls from first row to last on the most constrained supported
  board.
- **SC-002**: No book in the 2,899-book reference corpus costs more than **20% of the device's
  measured usable heap** (222,180 bytes) in chapter metadata — down from 44.9% today — and the
  figure is flat as a file's section count grows past the ceiling. Measured once, at spec time
  (`research.md` M1–M3, reproducible per `quickstart.md` §6); CI guards it by proxy through the
  host allocation budget, since the corpus is not in the repository.
- **SC-003**: Chapter-list memory is constant with respect to chapter count — a fixed row window,
  so a 4-chapter book and a book at the ceiling hold the same buffers. Verified by inspection of
  the row buffers and by a device heap check: host tests cannot reach activities, and a host-only
  guard for activity memory would pass on host while being dead on device.
- **SC-004**: The reference anthology (1.9 MB, 66 chapters) shows the same chapters in the same
  order, at the same progress percentages, as it does today.
- **SC-005**: An FB2 with a cover opens on that cover; one page turn reaches the first page of
  text; a book reopened from a position saved before this change lands on exactly the text it
  left, in 100% of tested positions.
- **SC-006**: No *page* cache (`sections/<n>.bin`) written before this feature is invalidated by
  it, and no reading position is lost. The book-metadata cache is rebuilt once per book, by one
  metadata pass on first open, to carry the derived-label marker — no book is re-paginated.
- **SC-007**: In the reference anthology's chapter list, zero rows show the placeholder where
  the section has text of its own.
- **SC-008**: A malformed chapter cache, a truncated `cover.bmp`, and a cover that lies about
  its dimensions each produce a bounded failure with no crash and no sanitizer finding. The
  bitmap half is already guarded by `test/gfx_renderer/BitmapTest.cpp`; what this feature adds is
  the reader's response to that rejection (FR-010).

## Out of Scope

- **Issue #8 — FB2 chapter metadata on SD with a seekable LUT.** This is the structural fix that
  would delete the ceiling entirely and make chapter-list memory independent of book size, and
  it remains the documented upgrade path. It is several times the size of the nested-chapter
  change: a new `book.bin` layout with an offset table, a seek path for title lookups, a cache
  version bump, and a rework of the accessors that currently hand back references into the
  in-memory chapter vector. Per the constitution, a refactor of that size opens a Discussion
  first. Bounding the ceiling (User Story 1) protects the device today and is a stepping stone
  to that work, not a replacement for it — FR-007's single source of truth is what makes the
  later deletion cheap.
- **Issue #4 — background prefetch of the next chapter's page cache.** Cross-format, and a
  concurrency design on a target with exactly one application task and a recursive storage
  mutex shared with the render path. Its open questions (a second task on a single-core board,
  a second in-flight section build's memory budget, how a prefetch yields to a page turn,
  whether it applies to EPUB) are not answerable inside a memory-and-labels feature, and folding
  it in would block everything here behind them. Needs its own Discussion and spec.
- **Issue #9 — body-level front matter carries no progress weight.** Accepted limitation, not a
  defect to fix. "A chapter is a section's own direct content" is the single rule that makes the
  progress calculation exactly partition the body; giving body-level front matter its own
  chapter would add a list row to every book that has any. The affected quantity is a few
  hundred bytes of a 1,827,705-byte body — far below one displayed percent. It stays recorded
  in the code with its reason.
- **Inline FB2 images beyond the cover.** Decoding and placing arbitrary images inside the text
  flow on a ~380 KB device is a separate problem from displaying one already-extracted bitmap.
- **Any change to EPUB, TXT, or XTC reading.** The chapter-list windowing this feature adopts is
  copied from EPUB's existing behaviour, not changed in it.

## Assumptions

- The issues listed are the complete set of open issues on the fork as of 2026-09-20 (seven,
  fetched from the repository); upstream's own issue tracker is a separate backlog and is not
  in scope.
- "Most constrained supported target" means the ESP32-C3 (X4/X3) at ~380 KB usable RAM, with a
  48 KB framebuffer and ~56 KB of static allocation before any book opens.
- The reference anthology (1.9 MB, 66 chapters, ~15 KB of chapter metadata) remains the
  calibration book; it is comfortable today and must stay so.
- The chapter-count distribution of the 2,899-book reference corpus (median 18, p90 64, p99 200,
  22 books above 256) is representative of what readers own. A ceiling that degrades 0.76% of
  books — all of them reference works, not novels — is an acceptable price for halving the worst
  case. FR-003 means a degraded book still opens and still reads; measured, it puts a median
  31.5% of its text into one tail chapter, which is coarser navigation and a slower first
  pagination of that chapter, not lost content.
- The cover asset is already produced at import for every FB2 that has one; this feature
  consumes it and does not change extraction.
- A cover page that occupies a position ahead of the first page of text — rather than being
  numbered as a page of the first chapter — is what satisfies FR-011 and FR-012 without a cache
  bump; the mechanism is a planning decision.
- Untitled sections are common enough in nested anthologies to be worth labelling, and a book's
  own first line is a better label than any generated text.
- Device figures (99,716 bytes worst case, 222,180 bytes usable heap, 36 bytes per chapter) come
  from a firmware build's RAM report, `sizeof` values read out of a riscv32 object file, and the
  2,899-book corpus parsed through this repo's own metadata parser; the method is recorded in
  `research.md` (M1–M3). Host allocation figures from `test/fb2_book/Fb2BookTest.cpp` remain the
  regression proxy that guards SC-002 and SC-003 in CI.

## Dependencies

- `003-fb2-nested-chapters` is landed; its chapter contract (C1–C10) and `FB2_CACHE_VERSION = 3`
  layout are the baseline this feature modifies.
- The existing FB2 cover extraction path and the bitmap scaling the sleep screen already uses.
- The host test program (`bin/run-tests`) and its FB2 fixture generators, which are how SC-002,
  SC-003, SC-006 and SC-008 are verified without hardware.
- Hardware or simulator verification for anything display-facing: the cover page and the chapter
  list are rendering changes and cannot be proven on host alone.
