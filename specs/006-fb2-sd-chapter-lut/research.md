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

**Decision**: the only ceiling left is **32,767** (`INT16_MAX`): 18× the largest real book.
The `u16` fields would allow 65,535, but the UI chapter list stores each row's value and the
selection as `int16_t` (`freeink-sdk/libs/ui/FreeInkUI/include/components/lists/list.h:15,63`),
and `Fb2ReaderChapterSelectionActivity.cpp:75` casts to it, so a higher chapter could not be
selected. Past it, a nested section still belongs to the chapter that contains it. A top-level
section past the limit has no parent, so its bytes land in no chapter and its text is
unreachable (`Fb2MetadataParser.cpp:270-280`). That is the documented ceiling.

**Why it matters**: kept in RAM, the 1,772-chapter book would cost about 64 KB of
`SectionInfo` (36 B each on riscv32) plus its long-title heap blocks. That exceeds the
~138 KB free at Home once the title heap is counted. So removing the cap without moving the
data is not an option (spec US2).

**Re-measured after implementation (2026-09-22, SC-001):** the same tool, built against the
real `lib/Fb2` on `feature/fb2-sd-chapter-lut` with no patch, loads all 2,899 books with no
failures. The largest still lists 1,772 chapters and 22 books have more than 256. For every
book, the chapter count, the deepest level and the total title bytes are identical to the
uncapped in-RAM measurement above (0 differences).

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

## R10. Device measurements (T001 baseline, T044 validation)

Measured on the Xteink X4 (ESP32-C3, 160 MHz) on 2026-09-22 with a throwaway boot-time harness,
since deleted. The same harness ran on `master` (T001) and on this branch (T044) against the
same SD card: 944 FB2 books and 2 EPUBs. The 1,772-chapter corpus book is **not** on the card,
so the eight largest FB2 files stood in for it. Two of them were pinned at the old cap.

| Measurement | `master` (book.bin v4) | this branch (v5) |
|---|---|---|
| Largest-chapter book ("Звездный ковчег", 17.3 MB) | 256 chapters (capped) | **677 chapters** |
| Second capped book (Bradbury omnibus, 11.8 MB) | 256 chapters (capped) | **343 chapters** |
| Heap held by that open book | 21,552 B | **432 B** |
| Heap held by a 26-chapter book | 3,608 B | 584 B |
| Lowest free heap during the run (first opens included) | 100,092 B | **132,744 B** |
| First open, largest book, 3 runs | 31,767 / 31,775 / 31,768 ms | 42,351 / 40,460 / 40,460 ms |
| First open, 26-chapter 24 MB book, 3 runs | 44,982 / 44,982 / 44,986 ms | 45,409 / 45,415 / (run 3 not captured) ms |
| Chapter-list window (24 entries), largest book | in RAM, ~0 ms | mean 66 ms, max 72 ms (29 windows) |
| Chapter-list window, 26-chapter book | ~0 ms | mean 30 ms, max 51 ms |
| EPUB window, same card (51 TOC entries) | mean 33, max 47 ms | mean 33, max 48 ms |
| Free heap across a full chapter-list scroll | — | flat (149,828 B throughout) |

**Migration (US3), on real v4 caches written by `master`:** a stand-in layout file was left in
`sections/` for all eight books. After the first open on this branch, it survived in the six
books with ≤256 chapters and was removed in the two that grew past 256.

**Titles past the old cap (US1 scenario 4):** chapters 256, 300 and 676 of the 677-chapter book
read back their own titles ("Благодарности", "42 Старший", "Владимир Марышев «ТЕНИ ПРОШЛОГО»").
This was checked in the log only; the status bar was not photographed.

**Findings against the success criteria**

- **SC-003 (heap) met.** A book with 677 chapters holds 432 B, against 21,552 B for the same book
  capped at 256 on `master`. The indexing peak is 32,652 B lower.
- **SC-005 (chapter list vs EPUB) not met as written.** A 24-entry FB2 window takes 66 ms mean,
  against 33 ms for this card's EPUB. The EPUB baseline has only 51 entries, far short of the
  ~1,000 the criterion assumes, and both are small next to a ~1 s panel refresh. Each FB2
  lookup does two seeks (the record, then the title elsewhere in the file). Reading the 24
  records in one 480-byte read would halve them.
- **R2 trigger fired: first open is slower.** The 677-chapter book takes ~8.7 s (27%) longer to
  index. That is about 4 ms per unbuffered temp-file write, 3 writes per chapter, consistent
  with sector-cache contention against the source reads. The 26-chapter book is ~430 ms (1%)
  slower, outside a run-to-run spread of ≤6 ms. This is the planned follow-up: buffer
  `titles.tmp` (append-only).
