# Implementation Plan: FB2 Chapter Metadata on SD

**Branch**: `feature/fb2-sd-chapter-lut` | **Date**: 2026-09-22 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/006-fb2-sd-chapter-lut/spec.md`

## Summary

Move FB2 chapter metadata from `std::vector<SectionInfo>` in RAM to fixed 20-byte records in
`book.bin`. Record *i* sits at a computed offset, and the titles are packed after the
records. The first parse streams records to two temp files. A single pass then assembles
`book.bin` and fills in the running byte totals, so progress needs one record, not a scan.
Lookups return `SectionInfo` by value. The reader caches the current chapter's record, so page
turns do no index I/O. `FB2_MAX_CHAPTERS` is deleted. The only ceiling left is 32,767,
set by the UI list's `int16_t` row index, which is 18× the largest of 2,899 real books (1,772, research R1).

Ponytail scope: the same struct, the same accessor names, the same TOC identity, the same
`progress.bin` and the same section files. No code is shared with EPUB, and no new
abstraction is added beyond a two-callback parser sink.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), `-fno-exceptions`, no RTTI

**Primary Dependencies**: in-tree expat (`XML_GE=0`, `XML_CONTEXT_BYTES=1024`), `lib/Serialization`, `HalStorage`/`HalFile`

**Storage**: SD, `/.crosspoint/fb2_<hash>/book.bin` v5 ([contract](contracts/book-bin-v5.md)); temp files `chapters.tmp` and `titles.tmp` exist only during a build

**Testing**: host gtest via `bin/run-tests` (plain and `--asan`). Suites: `fb2_book`, `fb2_metadata_parser`, `fb2_section_cache`, `xtc_fb2_readers`, over the POSIX `HalStorage` stub in `test/fb2_common/stubs`

**Target Platform**: ESP32-C3 (`default`) and the ESP32-S3 boards. The change is board-independent: there is no PSRAM path and no `BoardConfig` gate.

**Project Type**: embedded firmware: `lib/Fb2` (host-compilable) and `src/activities/reader`

**Performance Goals**: zero index reads per page turn (SC-004). A chapter-list window refresh opens the file once, and takes no longer per screen than EPUB (SC-005). A percent jump takes ⌈log₂ n⌉+1 record reads.

**Constraints**:
- per-chapter RAM is 0 after load and 0 at peak during the parse, beyond the open-section stack (max depth 7 in the corpus);
- no member file handle (research R3);
- stack locals under 256 B (the copy buffer is 128 B).

**Scale/Scope**: real books run up to 1,772 chapters and 83,658 B of titles (R1); the ceiling is 32,767 chapters. About 8 files touched plus docs and tests.

## Constitution Check

*GATE: checked before Phase 0 research and again after Phase 1 design.*

| Principle | Status | Evidence |
|---|---|---|
| I. Focused reading device | PASS | Makes every chapter of the 22 capped books reachable, and removes a RAM-driven limit. No new UI or scope. |
| II. Memory is the constraint | PASS | Removes the O(n) vector: 43,732 B for the worst book at the cap, and about 150 KB if the cap were removed with no other change (R1). New heap use is transient only: one `std::string` per lookup, and one `SectionInfo` per open section while parsing (in place of n). There are no `push_back` loops over chapters. The copy buffer is on the stack at 128 B. |
| III. Portability behind the HAL | PASS | All I/O goes through `HalFile`/`HalStorage`. `lib/Fb2` stays host-compilable. The stub already supports seek then overwrite in `"wb"` mode, matching the device's `O_RDWR` open (`SDCardManager.cpp:385`). |
| IV. Evidence over claims | PASS | Every number cites R1 (the corpus), the device note (138 KB free), or the UI list's `int16_t` index (`list.h:15,63`). SC-002's allowance is defined as 0 for books of equal depth, not guessed. First-open cost is measured, not assumed (quickstart device step 3). |
| V. Tests prove behavior | PASS (obligation) | Each FR maps to a host test in the quickstart. The malformed v5 corpus is generated in the test. Each new test must show a mutation it catches. The existing cap tests flip into tests of the new ceiling. |
| VI. Untrusted input | PASS | An exact file-size equation, bounds on every record's title, checks on level, flags and cumulative totals, and a count checked against the file size before any read. No count drives an allocation. |
| VII. Upstream-first hygiene | PASS | FB2 is fork-only (`docs/file-formats.md` "FB2 caches *(fork-only)*"). Upstream has no overlapping PRs (searched `fb2`: only #755, the original fork PR, and #2484, closed). The work happens on a feature branch, with one logical change per commit. |

**Gate result**: PASS, no violations.

### Post-design re-check (after Phase 1)

Still PASS. The design adds two obligations, both captured in the contracts:
- **VI**: `titleLength` is `u16` but is capped at 4,096 on read. `chapterCount = 32768` must fail the
  count bound, and `32767` in a tiny file the size equation, both before any per-record read.
- **V/III**: the SC-004 test needs an open counter on the shared stub. It is added in the stub
  and counts on host exactly what the device does (every `openFileForRead`).

## Project Structure

### Documentation (this feature)

```text
specs/006-fb2-sd-chapter-lut/
├── spec.md
├── plan.md              # this file
├── research.md          # R1–R9
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── book-bin-v5.md
│   └── fb2-api.md
└── checklists/requirements.md
```

### Source Code (repository root)

```text
lib/Fb2/Fb2.h                         # drop vector + FB2_MAX_CHAPTERS; by-value lookups; ChapterSink
lib/Fb2/Fb2.cpp                       # v5 load/validate, sink → tmp files, assemble, sections/ drop
lib/Fb2/Fb2/Fb2MetadataParser.{h,cpp} # OpenSection owns its SectionInfo; emit via sink
lib/Fb2/Fb2/Fb2SectionParser.cpp      # lockstep counter against FB2_CHAPTER_INDEX_LIMIT
lib/Fb2/Fb2/Fb2Section.cpp            # by-value lookup (one line)
src/activities/reader/Fb2ReaderActivity.{h,cpp}                 # chapterInfo cache; batch percent jump
src/activities/reader/Fb2ReaderMath.cpp                         # lower-bound search
src/activities/reader/Fb2ReaderChapterSelectionActivity.{h,cpp} # one open per window
docs/file-formats.md                  # book.bin v5
test/fb2_book, test/fb2_metadata_parser, test/fb2_section_cache,
test/xtc_fb2_readers, test/fb2_common # updated and new tests, stub open counter
```

**Structure Decision**: the existing layout, with no new files in `lib/` or `src/`. The sink
struct is declared in `Fb2.h`, beside `SectionInfo`, because both parsers already include it.

## Commit plan (one logical change each, Principle VII)

> Superseded by the commit named at the end of each phase in [tasks.md](tasks.md) (four commits: the status-bar cache folds into v5, the batched reads into the cap removal). Kept as the original design record.

1. `refactor(fb2): parser emits chapters through a sink`: this commit has no format change.
   `Fb2` collects the sink into its vector. Parser tests switch to a collecting sink.
2. `refactor(fb2): keep chapter metadata in book.bin v5`: covers the format, load, assembly,
   by-value accessors, and the removal of the cap, plus docs and the malformed-input corpus.
   This is the core commit.
3. `perf(fb2): cache current chapter record in the reader`: covers the status bar and progress
   (SC-004).
4. `perf(fb2): one index open per chapter-list window and percent jump`: covers the batch
   overload plus binary search.
5. `fix(fb2): drop layouts built under the old chapter cap`: the R6 migration.

## Complexity Tracking

> No Constitution violations. Recorded here: the deliberate shortcuts and their ceilings.

| Decision | Why | Simpler / other alternative rejected because |
|---|---|---|
| Unbuffered temp-file writes during the parse (`ponytail:`) | Record slots must be patched in place, which a buffered writer can't do. It is 3 small writes per chapter. | Buffering `titles.tmp` is the upgrade path if first open is measurably slower on device (quickstart step 3). It is not added before a measurement shows the need. |
| Validate every record at every open | Keeps today's "corrupt → re-parse" guarantee: one sequential pass of 20 B reads, 35 KB for the largest real book. | Lazy per-lookup validation can't check the level sequence or the cumulative chain without the previous record. It would also turn corruption into mid-read failures instead of a clean re-parse. |
| Open per lookup, with a batch overload, instead of a member handle | Local handles can't race across the render and loop tasks (R3). | A member handle plus a mutex adds locking to fix a problem that doesn't exist with a local handle. |
| Drop `sections/` only for rejected pre-v5 caches of books with more than 256 chapters | Only those layouts have stale boundaries (R6). | Bumping the section version would lay out every FB2 book again (violates SC-006). |
| No shared code with `BookMetadataCache` | The formats differ in shape (spine and TOC with hrefs, versus a single chapter list), and the spec keeps this out of scope. | A common LUT abstraction would have two very different users and save little code. |
