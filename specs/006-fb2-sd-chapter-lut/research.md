# Research: FB2 Chapter Metadata on SD

All figures below were measured on 2026-09-22 unless marked otherwise.

## R1. How large do real books get once the cap is removed? (SC-001, Constitution IV)

**Measurement**: a throwaway host build of `lib/Fb2` with `FB2_MAX_CHAPTERS` patched to
65,535, run with `Fb2::load()` over all 2,899 books in `~/Calibre Library` (method from the
`fb2-corpus` note). Every book parsed.

| Metric | Value |
|---|---|
| Max chapters in one book | **1,772** (a culinary encyclopedia) |
| Books above 256 / above 1,024 | 22 / 2 |
| Max nesting level | 6 (so at most 7 sections open at once) |
| Largest per-book sum of title bytes | 83,658 |
| Longest single title | 1,144 bytes |

**Decision**: the only ceiling left is the `u16` chapter number, **65,535**: 37× the
largest real book. Past it the existing "extra sections belong to the chapter that contains
them" rule applies.

**Why it matters**: kept in RAM, the 1,772-chapter book would cost about 64 KB of
`SectionInfo` (36 B each on riscv32) plus its long-title heap blocks. That exceeds the
~138 KB free at Home once the title heap is counted. So removing the cap without moving the
data is not an option (spec US2).

## R2. Where are records written during the first parse?

The parser learns a chapter's fields at three moments. The start tag gives the source offset
and depth. The title or first paragraph arrives later. The own length is known only at the end
tag. End tags arrive in post-order (children before parents), but records are numbered in
start-tag order.

**Decision**: use two temp files in the book's cache dir.
- `chapters.tmp`: fixed 20-byte records. A zeroed slot is **appended** at each start tag
  (appending never has to seek past EOF). The slot is **patched in place** at the end tag.
- `titles.tmp`: title bytes appended at each end tag.

After the parse, one sequential **assembly** pass writes `book.bin` in this order: the header;
then the records, filling in `cumulativeLength` and the real title offsets along the way; then
a copy of the titles through a 128-byte stack buffer. Both temp files are then removed.

**Rationale**: while the parse runs, it holds nothing per chapter except the open-section
stack (at most 7 deep in the corpus). That stack already exists; each entry now also carries
the section's pending title. EPUB builds `book.bin` from temp files the same way
(`BookMetadataCache.cpp:16-17`).

**Alternatives rejected**:
- *Keep the vector during the parse and stream it out at the end.* That moves the peak to
  the first open instead of removing it (violates FR-003).
- *Write records in end-tag order and reorder them during assembly.* Reordering needs either
  an index in RAM or a random-write pass plus a second pass for cumulative totals. It is more
  code for the same result.
- *Write straight into `book.bin` with no temp files.* The header size depends on
  `<description>`, which must come before the body in valid FB2 but may not in real files.
  The cumulative totals would still need a second pass.

**Cost to watch**: each chapter takes three small SD writes that interleave with reads of the
source file. That contends for SdFat's single shared sector cache (see the note in
`BookMetadataCache.h`). The quickstart times the first open of the reference book against
current firmware. The upgrade path is a `BufferedFileWriter` on `titles.tmp`, which is
append-only. This will be marked with a `ponytail:` comment.

## R3. Keep one file handle open, or open per lookup?

`BookMetadataCache` keeps a member `bookFile` open. For FB2 that would make seek-then-read on
a shared handle a race between the render task and the loop task. For example,
`jumpToPercent` looks chapters up outside `RenderLock` (`Fb2ReaderActivity.cpp:288`).

Opening by path isn't free: SdFat scans `/.crosspoint`, which holds one directory per cached
book, on every open. So a 24-row chapter-list window that opened the file once per row would
scan that directory 24 times.

**Decision**: never keep a handle as a member. Every lookup has two overloads:
- one that opens `book.bin` for a single lookup;
- one that takes a caller-owned `HalFile&` that is already open, for batches.

The chapter-list window refresh and `jumpToPercent` each open the file once per batch.

**Alternative rejected**: a member handle plus a mutex. It adds locking to fix a problem that
a local handle doesn't have.

## R4. Status bar and progress on every page turn (FR-007, SC-004)

`renderStatusBar()` reads the chapter title and `calculateProgress()` on every render
(`Fb2ReaderActivity.cpp:660-674`). Reading from SD there breaks SC-004. Worse, it would take a
`c_str()` from a temporary returned by value and use it after the temporary is gone (the
FR-013 hazard).

**Decision**: the reader keeps one `Fb2::SectionInfo chapterInfo` along with the index it was
read for. At the top of `renderBook()`, if that index no longer matches `currentSectionIndex`,
it re-reads. This is a single check, and it corrects itself however the index changed; there
are 8+ places that assign it. `calculateProgress` takes the cached record, not an index. The
status bar reads `chapterInfo.title`.

## R5. Percent jump over an SD-resident index

`percentToSection` scans linearly through a cumulative-size callback
(`Fb2ReaderMath.cpp:26-35`). Today `getCumulativeSectionSize` itself loops over every chapter,
so the jump is O(n²) in RAM. With a stored `cumulativeLength` each callback is one record
read, but a linear scan would still be n reads.

**Decision**: change the loop to a lower-bound binary search. Cumulative sizes never decrease,
because lengths are never negative. That makes the jump about ⌈log₂ n⌉+1 reads: at most 12
for the largest real book. It is pure math, and the host tests for it already exist. The
callback context carries one open `HalFile`.

## R6. Saved layouts for books that were capped (FR-010, SC-006)

Section files are named by chapter index and validated only by version and render spec
(`Fb2Section.cpp:88-114`). For a book at or below 256 chapters the numbering and every
chapter's boundaries are the same, so its files stay valid. For a book above the old cap,
every ancestor of a section numbered 256 or higher used to include that section's text. Now
it doesn't, so those layouts are stale.

**Decision**: `load()` removes `sections/` when both of these hold:
- the cached `book.bin` it rejected had an older version (earlier than 5);
- the fresh parse finds more than 256 chapters.

Measured impact: 22 books in the corpus (0.76%) lay out again, once. Their saved position
still resolves. The chapter index keeps its meaning for every chapter below 256, and
`renderBook()` already clamps the page into range. The progress format doesn't change.

**Alternative rejected**: bump `FB2_SECTION_FILE_VERSION`. It would lay out every FB2 book
again to fix 0.76% of them (violates SC-006).

## R7. Validation cost at open (FR-011, Constitution VI)

Today `loadMetadataCache` checks every entry. **Decision**: keep checking every record at
load, in one sequential pass of 20-byte reads. The largest real book reads 35 KB this way.
Title bytes are not read at load. Instead, the check that `titleOffset + titleLength ≤
titlesSize` bounds every later title read, and `fileSize == titlesBase + titlesSize` catches a
truncated file in O(1).

## R8. Parser output API

`Fb2MetadataParser` currently owns the vector and hands it over through `takeSections()`.
**Decision**: the parser writes through a two-function-pointer `ChapterSink` (`reserve`,
`write`). This follows the codebase's callback style (`Fb2PageCompleteFn`, `SectionSizes`);
CLAUDE.md rules out `std::function`. `Fb2` supplies the sink that writes the temp files. Parser
tests supply one that collects into a vector, and that vector is test-only memory. This keeps
the parser host-testable without a filesystem.

## R9. Chapter-list glyph prewarm

EPUB batches `prewarmFallbackText` on each window refresh. FB2 skipped it "because its entries
are already in RAM", and that reason no longer holds. **Decision**: still skip it. Prewarm is
about CJK fallback glyphs, not SD latency, and nothing shows FB2 lists repainting slowly.
Update the `ponytail:` comment's reason and keep its upgrade path.
