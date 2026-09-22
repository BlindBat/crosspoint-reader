# Feature Specification: FB2 Chapter Metadata on SD

**Feature Branch**: `feature/fb2-sd-chapter-lut`

**Created**: 2026-09-22

**Status**: Draft

**Input**: GitHub issue [BlindBat/crosspoint-reader#8](https://github.com/BlindBat/crosspoint-reader/issues/8) — "refactor: move FB2 chapter metadata to SD with a seekable LUT". User description: "with /ponytail full issue #8" (minimum change that closes the issue).

## Summary

An open FB2 book keeps every chapter's title, position, length and depth in RAM for as
long as it is open. Because that cost grows with the book, the chapter count is capped at
256 (`lib/Fb2/Fb2.h:21`), and past the cap further sections stop being chapters: the text
is still read, but it cannot be reached from the chapter list. Measured across the
2,899-book corpus, that cap coarsens navigation in 22 books (0.76%), and the worst book
still holds 43,732 bytes of chapter metadata at 256 — about a third of the ~138 KB the C3
has free at Home (measured on device, 2026-09-20).

This feature keeps chapter metadata on the SD card and reads one chapter's entry when it
is needed, the way EPUB already does. The chapter cap goes away, and the memory an open
FB2 book holds for its chapter list stops depending on how many chapters it has.

It is an internal change. A reader should see exactly one difference: books that used to
be capped now list every chapter.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Every chapter of a large book is reachable (Priority: P1)

A reader opens an FB2 anthology or reference book with more than 256 sections. The chapter
list shows every section as its own entry, at its own depth, and selecting any of them
opens that chapter.

**Why this priority**: This is the only user-visible defect the issue fixes, and it is
what the cap currently costs real books.

**Independent Test**: Open one of the corpus books with more than 256 sections (or a
generated book with 1,000+). Scroll to the end of the chapter list, pick the last entry,
and confirm that chapter's text opens.

**Acceptance Scenarios**:

1. **Given** a book with more than 256 sections, **When** the reader opens the chapter list, **Then** there is one entry per section, in document order, indented by depth.
2. **Given** that list, **When** the reader selects an entry past the 256th, **Then** that section's own text opens, not the text of an earlier chapter that contains it.
3. **Given** a book with more than 256 sections, **When** the reader reads it straight through, **Then** book progress rises steadily from 0% to 100% with no jumps or reversals, and no text is lost or repeated.
4. **Given** the status bar is set to show the chapter title, **When** the reader is in a chapter past the 256th, **Then** that chapter's own title is shown.

---

### User Story 2 - Open-book memory does not grow with the book (Priority: P1)

A reader opens a very large FB2 book on the C3. It opens, the chapter list opens, and the
reader turns pages with the same headroom as a small book.

**Why this priority**: Equal first. Removing the cap without moving the data would turn a
coarse-navigation problem into an out-of-memory crash, which is worse. Stability outranks
navigation.

**Independent Test**: On the host, count what a book holds in memory after opening for a
4-chapter book and for a 4,000-chapter book; the two must be within a fixed allowance of
each other. On device, open the largest corpus book and read `[MEM] Free` over serial.

**Acceptance Scenarios**:

1. **Given** two books that differ only in chapter count, **When** each is opened, **Then** the memory held for chapter metadata after opening is the same for both, within a fixed allowance.
2. **Given** a book being opened for the first time (no cache), **When** its chapters are indexed, **Then** peak memory during indexing also does not grow with chapter count.
3. **Given** the chapter list is open on a very large book, **When** the reader scrolls from top to bottom, **Then** memory in use stays flat.

---

### User Story 3 - Nothing else changes for existing books (Priority: P2)

A reader who already has FB2 books on the device updates the firmware. Their books open
where they left off, and books that were under the cap are not laid out again.

**Why this priority**: The change is internal; it must not cost the reader anything they
already had.

**Independent Test**: With the previous firmware, open and read into several FB2 books
(under and over the cap), note positions, update the firmware, and reopen each.

**Acceptance Scenarios**:

1. **Given** a book with 256 or fewer chapters and a saved position, **When** it is opened after the update, **Then** it opens at the same chapter and page, and its already laid-out chapters are reused rather than rebuilt.
2. **Given** a book that exceeded the old cap, **When** it is opened after the update, **Then** it opens without error at the saved chapter, and any chapter whose content boundaries changed is laid out afresh rather than shown from a stale layout.
3. **Given** any FB2 book, **When** it is opened the first time after the update, **Then** its chapter index is rebuilt once and not again on later opens.
4. **Given** page turns within a chapter, **When** the reader turns pages, **Then** they are as fast as before; chapter metadata is not re-read on every turn.

### Edge Cases

- **Corrupt or truncated cache**: a damaged chapter index (bad count, entry past the end of the file, out-of-sequence depth, invalid flags) is rejected and the book is re-indexed from the source file, as today. A bad entry never drives an unbounded allocation.
- **Card removed or read fails mid-read**: a chapter entry that cannot be read shows the localized "Unnamed" placeholder in the list or status bar and does not crash; opening that chapter fails the way an unreadable section does today.
- **Book with no `<section>` at all**: still reads as one whole-file chapter.
- **Pathological section count**: the only remaining limit is the width of the stored chapter number (65,535). Past it, the existing rule applies — further sections read as part of the chapter containing them, so no text is lost.
- **Very long titles**: a supplied title is kept as written, bounded by the existing 4,096-byte string cap; derived labels keep their 64-character cap.
- **Old saved positions** that use top-level-only numbering (pre-nested-chapter firmware) still resolve to the right chapter.
- **Low SD space on first open**: if the index cannot be written, the book fails to open with the existing error path rather than opening half-indexed.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Every `<section>` in a reading body MUST be its own chapter, numbered in document order at any depth, up to the width of the stored chapter number (65,535). The 256-chapter ceiling (`FB2_MAX_CHAPTERS`) MUST be removed, not raised.
- **FR-002**: Chapter metadata (title, derived-title marker, source position, own length, depth) MUST be stored on the SD card and read per chapter on demand; an open book MUST NOT hold a per-chapter record in memory.
- **FR-003**: Memory held by an open book for its chapter metadata MUST be independent of chapter count, both after opening and at peak while the book is first indexed.
- **FR-004**: Looking up one chapter's metadata, and the book progress for a position, MUST each cost a bounded number of SD reads, independent of chapter count and of which chapter is asked for.
- **FR-005**: Both FB2 parsers (indexing and chapter layout) MUST continue to number chapters identically, so the chapter a reader selects is the text that opens.
- **FR-006**: The chapter list MUST keep its fixed window of rows and read only the entries in view; scrolling it MUST NOT accumulate memory.
- **FR-007**: The chapter title shown in the status bar MUST be read once per chapter change, not on every page render.
- **FR-008**: Reading progress MUST stay monotonic across the whole book, and chapter lengths MUST continue to partition the reading bodies (no byte counted twice or not at all).
- **FR-009**: The cache format change MUST bump the FB2 metadata cache version so any existing cache is rebuilt once; `docs/file-formats.md` MUST be updated in the same change.
- **FR-010**: For books with 256 or fewer chapters, saved reading positions and already laid-out chapters MUST remain valid after the update. For books over the old cap, positions MUST resolve without error and no chapter MAY be shown from a layout built under the old, capped boundaries.
- **FR-011**: Every value read from the index MUST be validated before use (count against file size, entry bounds, depth sequence, flag bits, string length), and any failure MUST fall back to re-indexing the source file.
- **FR-012**: A failed chapter-entry read at display time MUST degrade to the localized placeholder title, never crash or show garbage.
- **FR-013**: The accessors that today return references into the in-memory list MUST no longer do so; every caller (`Fb2ReaderActivity`, `Fb2ReaderChapterSelectionActivity`, `Fb2Section`) MUST be updated to the new lookup, with no caller left holding a pointer into memory that is freed or overwritten on the next lookup.

### Key Entities

- **Chapter index**: the per-book file on SD that replaces the in-memory chapter list. Holds the book's title/author/language/cover id, the chapter count, and one fixed-position lookup slot per chapter pointing at that chapter's entry.
- **Chapter entry**: one chapter's title, derived-title marker, source position, own length, depth, and the running total of own lengths up to and including it (so progress needs one entry, not a scan).
- **Chapter number**: the index shared by the chapter list, the per-chapter layout files and saved progress. Unchanged in meaning; only its ceiling moves.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every corpus book that the old cap degraded (22 of 2,899) lists exactly as many chapters as the indexing parser finds sections, re-measured by running the parser uncapped over `~/Calibre Library`.
- **SC-002**: On the host, memory held for chapter metadata after opening a 4-chapter book and a 4,000-chapter generated book differs by no more than a fixed allowance that does not scale with chapter count (host allocation counts are the accepted proxy, Constitution IV). Peak memory during first indexing meets the same bound.
- **SC-003**: On the C3, opening the largest corpus book leaves at least as much free heap as opening the worst book at the old cap does on current firmware (baseline: 43,732 bytes of chapter metadata; measured via `[MEM] Free` over serial before and after).
- **SC-004**: Page turns within a chapter perform zero chapter-index reads (host-countable).
- **SC-005**: Scrolling the chapter list of a 1,000+ chapter book from top to bottom on device takes no longer per screen than the EPUB chapter list does for a book of similar chapter count (both timed on the same device over serial), and uses flat memory throughout.
- **SC-006**: For books with 256 or fewer chapters, a firmware update rebuilds only the chapter index: no chapter is laid out again and every saved position reopens at the same chapter and page.
- **SC-007**: All existing FB2 host suites pass, with the cap-specific tests replaced by tests of the new ceiling (65,535) and of books past the old cap.

## Assumptions

- **Minimum change, per the issue.** The FB2 chapter index follows the EPUB approach (entries on SD, a fixed-slot lookup table, one seek per lookup) but does not share code with `BookMetadataCache`. A common abstraction over both formats is out of scope: two differently shaped formats would share little, and nothing asks for it.
- **Out of scope**: any change to EPUB, TXT or XTC; chapter search or filtering; changing how chapters are defined (nested-section rules from spec 003 stand); new reader UI.
- **Chapter-list prewarm**: the FB2 list currently skips EPUB's fallback-glyph prewarm because its entries were in RAM (`Fb2ReaderChapterSelectionActivity.cpp`, `ponytail:` comment). Adopting it is allowed but not required; decide in the plan from device timing.
- **The 65,535 ceiling** is the width of the chapter number already stored in `progress.bin` and the index count (`docs/file-formats.md:651`), not a memory budget, so it needs no new measurement. The uncapped corpus maximum is still re-measured (SC-001) to confirm no real book approaches it.
- **Supersedes** issues #6 and #7 (both closed; #7's windowing already landed). Closing #8 also removes the `ponytail:` comment on `FB2_MAX_CHAPTERS` and the corresponding Complexity Tracking row in `specs/003-fb2-nested-chapters/plan.md` becomes historical.
- SD storage cost of the index grows by a few bytes per chapter over the current `book.bin` (a lookup slot and a running total); this is negligible against the source file.
