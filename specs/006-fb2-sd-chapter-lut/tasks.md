---
description: "Task list for FB2 chapter metadata on SD (issue #8)"
---

# Tasks: FB2 Chapter Metadata on SD

**Input**: Design documents from `specs/006-fb2-sd-chapter-lut/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required (Constitution V). Within each story, tests are written first. Each new test must be seen failing against a deliberate mutation of the production code, and that mutation reverted before commit.

**Story order**: US2 comes before US1. Both are P1, but removing the cap before the data leaves RAM would turn coarse navigation into an out-of-memory crash (spec US2 "Why this priority").

**Commits**: one per phase from Phase 2 onward, in the semantic form named at the end of each phase. This replaces the plan's commit list. The plan's commits 3 and 4 fold into US2 and US1, because the status-bar change is needed for correctness as soon as lookups return values rather than references (FR-013).

## Format: `[ID] [P?] [Story] Description`

---

## Phase 1: Setup (test infrastructure)

- [ ] T001 Device baselines, **before any code change** (human, current `master` firmware): run `quickstart.md` device step 0 and record the three baselines in `specs/006-fb2-sd-chapter-lut/research.md` under a new R10. Steps 1–3 of T044 compare against them.
- [X] T002 [P] Extend `test/support/AllocCounter.{h,cpp}` with `liveBytes()` and `peakBytes()`. Prefix **every** block with a `max_align_t`-sized header that records its size, whether or not counting is enabled, because a block allocated before counting starts is still freed through the same `operator delete`. Only the counters are gated: subtract on delete, and track the high-water mark while counting is enabled. Add a self-check in `test/alloc_guards/AllocGuardsTest.cpp`: after allocating 100 B and freeing it, live is 0 and peak ≥ 100.
- [X] T003 [P] Add an open counter to `test/fb2_common/stubs/HalStorage.h`: `openForReadCount()` and `resetOpenCounts()`. `openFileForRead` increments the counter. Also add `failWritesAfter(size_t bytes)`: once that many bytes have been written across all files, `write` returns 0, so a test can make a build fail part-way. Reset clears both.
- [X] T004 [P] Add `writeV5BookBin(path, header strings, records, titles)` to `test/fb2_common/Fb2TestSupport.h`. It writes the byte layout in `contracts/book-bin-v5.md` field by field, so tests can hand-build valid and malformed caches.

---

## Phase 2: Foundational (the parser emits chapters through a sink; no format change)

**Purpose**: separate the parser from the in-RAM vector, while `book.bin` stays at v4 for now.

- [X] T005 Declare `struct Fb2ChapterSink { void* ctx; bool (*reserve)(void* ctx); bool (*write)(void* ctx, uint16_t index, const Fb2::SectionInfo& c); };` after `class Fb2` in `lib/Fb2/Fb2.h`.
- [X] T006 Rework `lib/Fb2/Fb2/Fb2MetadataParser.{h,cpp}`:
  - **State.** `OpenSection` gets `Fb2::SectionInfo info`, `uint16_t index` and `bool isChapter`, replacing `entryIndex`/`NOT_A_CHAPTER`. A `uint16_t chapterCount` counter replaces `sections.size()` at `:133`.
  - **Where titles and labels are written.** Point every write at `openSections.back().info` (`:154,166,240,253`).
  - **When the sink is called.** Call `sink.reserve` at the chapter start tag (`:137-144`). Call `sink.write(index, info)` with the final `length` in two places: at the end tag (`:273`), and for each still-open section when `</body>` clears the stack (`:285`, length stays 0 as today). Route the whole-file fallback (`:357-368`) through reserve+write.
  - **Failure.** A `false` from the sink calls `XML_StopParser(parser, XML_FALSE)`, and `parse()` returns false.
  - **API.** The constructor takes the sink. Delete `sections`, `getSections()`, `takeSections()` and `shrink_to_fit()`.
- [X] T007 In `lib/Fb2/Fb2.cpp` `parseMetadata()`, give the parser an interim sink that collects into `sections`: `reserve` pushes an empty entry and `write` assigns `sections[index]`. `book.bin` v4 output must stay byte-identical.
- [X] T008 [P] In `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`, add a collecting-sink helper (a test-only vector) and replace every `getSections()` use, including `parseSource` at `:408`. Add a test that a sink `write` returning false makes `parse()` return false. Mutation: ignore the return value.
- [X] T009 Run `bin/run-tests`. All suites must be green, unchanged. Commit `refactor(fb2): parser emits chapters through a sink`.

**Checkpoint**: behavior identical; the parser no longer owns chapter storage.

---

## Phase 3: User Story 2 — Open-book memory does not grow with the book (Priority: P1) 🎯 MVP

**Goal**: an open book, and a first parse, hold no per-chapter memory. `book.bin` v5 is on SD.

**Independent Test**: two books of the same depth, with 4 and 250 chapters, show equal `liveBytes()` after `load()` and equal `peakBytes()` during a first `load()`. The allowance is 0 (SC-002).

### Tests for User Story 2 (write first, must fail on the Phase 2 code)

- [X] T010 [P] [US2] In `test/fb2_book/Fb2BookTest.cpp`, replace `ChapterMetadataAllocatesOneTitlePerChapterAndStaysCapped` (`:351-390`) with `ChapterMemoryIsIndependentOfChapterCount`:
  - Build with `makeShortTitleTowerFb2`, so every title stays within SSO, for 4 and 250 chapters at the same depth.
  - Assert equal `liveBytes()` held after `load()`, and equal `peakBytes()` during a first `load()` with no cache.
  - On failure, print the delta. Do not widen the allowance without a note citing the reason.
- [X] T011 [P] [US2] v5 round-trip test in `test/fb2_book/Fb2BookTest.cpp`:
  - Parse, then reload from cache. Every `SectionInfo` field matches, including `cumulativeLength`.
  - `Σ length` equals the byte span of the fixture's top-level `<section>` elements, measured from the fixture string with `find("<section")` and the matching `</section>`. Do **not** compare against `getBookSize()` or the last `cumulativeLength`: both are that same sum, so the check could never fail. Mutation: drop the `childBytes` subtraction (`Fb2MetadataParser.cpp:273`).
  - The last `cumulativeLength == getBookSize()` stays as a separate format-consistency check.
  - `chapters.tmp` and `titles.tmp` are gone after a build.
- [X] T012 [P] [US2] Malformed v5 corpus test in `test/fb2_book/Fb2BookTest.cpp`, using `writeV5BookBin`. Each case must be rejected, after which `load()` re-parses and writes a valid v5 file:
  - title area truncated by 1 byte;
  - `titlesSize` off by ±1;
  - `titleOffset + titleLength > titlesSize`;
  - `titleLength = 4097`;
  - wrong `cumulativeLength`;
  - `cumulativeLength` overflowing u32;
  - `level` jump of 2;
  - `level != 0` on record 0;
  - flags `0x02`;
  - derived flag on an empty title;
  - `chapterCount = 0`;
  - `chapterCount = 32768`, one above `FB2_CHAPTER_INDEX_LIMIT`;
  - `chapterCount = 32767` in a 100-byte file;
  - a header string length of 4097.

  Port the existing v4 corrupt-cache tests (`:263-313`) to v5 bytes.
- [X] T013 [P] [US2] Failed-build test in `test/fb2_book/Fb2BookTest.cpp`, using the stub's `failWritesAfter`. Fail writes once during the parse and once during assembly. Each time, `load()` must return false and leave no `book.bin`, `chapters.tmp` or `titles.tmp` in the cache dir (spec edge case "Low SD space"). Mutation: skip the cleanup in T018.
- [X] T014 [P] [US2] In `test/fb2_book/Fb2BookTest.cpp`, rewrite the `Fb2MathTest` fixture (`:597-605`). It should `writeV5BookBin` three records (One/0/100/lvl0, Two/100/300/lvl1, Three/400/600/lvl0) and `load(false)`, instead of assigning `book.sections`. Convert `calculateProgress(i, f)` calls to `calculateProgress(book.getSectionInfo(i), f)`. Expected values do not change. Add a test that an out-of-range index and a lookup against a missing `book.bin` return an empty `SectionInfo` (FR-012).
- [X] T015 [P] [US2] Add a read-failure test in `test/fb2_book/Fb2BookTest.cpp`. After `load()`, truncate `book.bin` on disk. `getSectionInfo(i)` must then return an empty title and must not crash.

### Implementation for User Story 2

- [X] T016 [US2] Edit `lib/Fb2/Fb2.h` to match `contracts/fb2-api.md`:
  - Remove `std::vector<SectionInfo> sections`, and add `uint16_t chapterCount`, `uint32_t recordsOffset`, `uint32_t titlesOffset` and `uint32_t bookSize`.
  - Add `size_t cumulativeLength = 0;` to `SectionInfo`.
  - Make `getSectionInfo`, `getTocEntry` and `getCumulativeSectionSize` return by value, each with a `HalFile& bookBin` overload. Add `bool openIndex(HalFile&) const`.
  - Change the signature to `calculateProgress(const SectionInfo& chapter, float chapterRead) const`.
  - `FB2_MAX_CHAPTERS` stays for now; US1 removes it.
- [X] T017 [US2] In `lib/Fb2/Fb2.cpp`, write the build sink that replaces T007's interim one. It writes to `cachePath + "/chapters.tmp"` and `"/titles.tmp"`:
  - **`reserve`** appends a zeroed 20-byte record at `count·20`.
  - **`write`**:
    1. Seek to `index·20`.
    2. Append the title to `titles.tmp`, clamped to `FB2_CACHE_MAX_STRING` (4,096) bytes and cut back to a UTF-8 lead byte, so a valid parse can never produce a file that fails validation.
    3. Write the record field by field (`u32 titleOffset, u16 titleLength, u32 fileOffset, u32 ownLength, u32 cumulativeLength=0, u8 level, u8 flags`).
    4. Seek back to the end.
  - Add the `// ponytail:` comment "unbuffered: slots are patched in place; buffer titles.tmp (append-only) if first open measures slower on device" (research R2).
- [X] T018 [US2] In `lib/Fb2/Fb2.cpp`, rewrite `saveMetadataCache()` as the assembly pass:
  1. Write `u8 5`, the 4 header strings, `u16 chapterCount` and `u32 titlesSize`.
  2. Read `chapters.tmp` record by record. Compute `cumulativeLength` as a running sum and fail on u32 overflow, then write each record.
  3. Copy `titles.tmp` through a `uint8_t buf[128]` stack buffer.
  4. Remove both temp files.

  On any failure, remove `book.bin` and both temp files and return false, so `load()` fails through its existing path. Set `FB2_CACHE_VERSION = 5` with a `// v5:` history line, and delete `FB2_CACHE_MIN_SECTION_ENTRY` and `countFitsRemainingFile`, which the size equation replaces.
- [X] T019 [US2] In `lib/Fb2/Fb2.cpp`, rewrite `loadMetadataCache()` for v5 using the data-model rules verbatim:
  1. `version == 5`; each header string ≤ 4,096 bytes.
  2. `1 ≤ chapterCount ≤ FB2_CHAPTER_INDEX_LIMIT` (use `FB2_MAX_CHAPTERS` until T030 renames it), and `recordsOffset + chapterCount·20 + titlesSize == fileSize` exactly, checked **before** any per-record read.
  3. For each record, in one sequential pass of `readPodChecked` calls:
     - `titleLength ≤ 4,096` and `titleOffset + titleLength ≤ titlesSize`;
     - `flags & ~0x01 == 0`, and a derived flag never on an empty title;
     - `level ≤ previous + 1`, and `level == 0` for record 0;
     - `cumulativeLength == previous + ownLength`, without overflow.

  Store `bookSize` as the last `cumulativeLength`. Title bytes are not read here.
- [X] T020 [US2] In `lib/Fb2/Fb2.cpp`, implement the accessors:
  - **`openIndex`** opens `book.bin` for reading.
  - **`getSectionInfo(i, bookBin)`**: seek to `recordsOffset + 20·i`, read the fields, then seek to `titlesOffset + titleOffset` and read `titleLength` bytes. Return an empty `SectionInfo`, with a `LOG_ERR`, on an out-of-range index or any short read.
  - **`getSectionInfo(i)`** opens the index and forwards.
  - **`getCumulativeSectionSize`**: the record read only.
  - **`getBookSize`** returns the member.
  - **`calculateProgress(chapter, f)`** returns `(chapter.cumulativeLength − chapter.length + f·chapter.length) / bookSize`, and 0 when `bookSize == 0`.
  - **`getTocEntry`** forwards.
  - **`firstChapterOfTopLevel`**: one `openIndex`, then a scan that reads only the `level` bytes.
  - **Range checks** use `chapterCount` instead of `sections.size()`.
- [X] T021 [US2] Update `lib/Fb2/Fb2/Fb2Section.cpp:214` to `const auto sectionInfo = fb2->getSectionInfo(sectionIndex);` (by value).
- [X] T022 [US2] In `src/activities/reader/Fb2ReaderActivity.{h,cpp}`:
  - **Members.** Add `Fb2::SectionInfo chapterInfo;` and `int chapterInfoIndex = -1;`.
  - **Refresh.** At the top of `renderBook()`, after the index clamps (`:392-397`), check `if (chapterInfoIndex != currentSectionIndex)`; if so, set `chapterInfo = fb2->getSectionInfo(currentSectionIndex); chapterInfoIndex = currentSectionIndex;`.
  - **Readers.** `renderStatusBar()` (`:660-674`) uses `chapterInfo.title`, the reader-owned copy. `renderStatusBar()` and `bookProgressPercent()` (`:123`) call `calculateProgress(chapterInfo, …)`.
  - **Reset.** Reset `chapterInfoIndex = -1` wherever the book is cleared or reloaded (`:247`).
  - Update the "Borrowed text only" comment (`:662`), because the title is now an owned copy.
- [X] T023 [US2] Update `src/activities/reader/Fb2ReaderChapterSelectionActivity.cpp:55` to `const auto tocEntry = fb2->getTocEntry(clamped + i);` (by value). US1 batches this.
- [X] T024 [US2] Replace the `book.bin` version 4 section of `docs/file-formats.md` (`:700-745`) with v5 from `contracts/book-bin-v5.md`. It should include the layout, the guarantees, the validation list and the version history line.
- [X] T025 [US2] Update the remaining callers in `test/fb2_book`, `test/fb2_section_cache` and `test/fb2_section_parser` so they compile against by-value accessors. `const auto&` binding a temporary is fine. Show the mutations for T010 (keep an unused `std::vector<SectionInfo>` that is filled at load), T012 (drop the exact-size equation) and T015 (skip the short-read check). Then run `bin/run-tests` and `bin/run-tests --asan`, and commit `refactor(fb2): keep chapter metadata in book.bin v5`.

**Checkpoint**: open-book memory is flat in chapter count. The cap still stands.

---

## Phase 4: User Story 1 — Every chapter of a large book is reachable (Priority: P1)

**Goal**: delete `FB2_MAX_CHAPTERS`, so the only ceiling is the u16 chapter number. Keep large lists and percent jumps cheap.

**Independent Test**: a generated 4,000-section tower lists 4,000 chapters, and chapter 3,000 lays out its own text.

### Tests for User Story 1 (write first)

- [X] T026 [P] [US1] In `test/fb2_book/Fb2BookTest.cpp`, flip the cap tests (`:279`, `:318-340`) into ceiling tests:
  - **Tower.** `makeSectionTowerFb2(4000, 2)` yields 4,000 chapters, and `Σ length` equals the fixture's top-level `<section>` span, measured with the same oracle as T011.
  - **Cached file.** A cached reload matches.
  - **Memory.** Extend T010 to 4 against 4,000 chapters (FR-003, SC-002).
- [X] T027 [P] [US1] In `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`, flip `:188-192`. Build the fixture inline: one top-level wrapper `<section>` holding `FB2_CHAPTER_INDEX_LIMIT + 2` flat child sections (about 1.3 MB). Then:
  - exactly 32,767 chapters are produced: the wrapper plus 32,766 children;
  - the last 3 children are not chapters, and their bytes are counted in the wrapper's own `length`, measured against the fixture string (C1 oracle);
  - no top-level section is used past the limit, because it would have no containing chapter (spec edge case, a documented ceiling).
- [X] T028 [P] [US1] In `test/fb2_section_cache/Fb2SectionCacheTest.cpp`, lay out chapter 3,000 of a 4,000-section tower. Its pages must hold that section's own title text and no neighbour's (FR-005 lockstep). Mutation: leave the section parser at 256.
- [X] T029 [P] [US1] In `test/xtc_fb2_readers/Fb2ReaderMathTest.cpp`:
  - `percentToSection` must return the same targets as the existing cases.
  - Over 4,000 sections, the cumulative callback must be called at most ⌈log₂ n⌉+2 times. Count the calls through the ctx.

### Implementation for User Story 1

- [X] T030 [US1] In `lib/Fb2/Fb2.h`, replace `FB2_MAX_CHAPTERS` and its comment block (`:9-21`, including the `ponytail:` note) with `static constexpr uint16_t FB2_CHAPTER_INDEX_LIMIT = INT16_MAX;`, commented: "Highest chapter the UI list can address: its row value and selection index are int16_t (FreeInkUI lists/list.h). Not a memory budget. Past it a nested <section> reads as part of the chapter containing it; a top-level one is unreachable." Update the uses in `lib/Fb2/Fb2/Fb2MetadataParser.cpp` (`:131-133`) and `lib/Fb2/Fb2/Fb2SectionParser.cpp` (`:13`, `:129`), and the corruption bound in `lib/Fb2/Fb2.cpp`.
- [X] T031 [US1] In `src/activities/reader/Fb2ReaderMath.cpp` (`:26-35`), change `percentToSection` from its linear scan to a lower-bound binary search over `cumulative(i)`. Cumulative sizes never decrease, so the result is the first `i` with `targetSize <= cumulative(i)`, and `prevCumulative = cumulative(i-1)` or 0.
- [X] T032 [US1] In `src/activities/reader/Fb2ReaderActivity.cpp`, change `jumpToPercent` (`:284-298`) and the `cumulativeSectionSize` helper (`:26-29`):
  - The ctx becomes a local `struct { const Fb2* fb2; HalFile* bookBin; }`, opened once with `fb2->openIndex`.
  - The callback uses `getCumulativeSectionSize(i, *bookBin)`.
  - If the open fails, fall back to returning with no jump.
- [X] T033 [US1] In `src/activities/reader/Fb2ReaderChapterSelectionActivity.{h,cpp}`:
  - **Batching.** `refreshTocWindow` opens the index once (`HalFile bookBin; fb2->openIndex(bookBin)`) and calls `getTocEntry(clamped + i, bookBin)` for the window.
  - **Header comment.** Drop the `FB2_MAX_CHAPTERS` mention (`.h:16`).
  - **Prewarm comment.** Rewrite the `ponytail:` prewarm comment (`.cpp:47-49`) per research R9, keeping the upgrade path: "no fallback-glyph prewarm; add EPUB's prewarmFallbackText if CJK FB2 lists repaint slowly on device".
  - **Timing.** Add a permanent `LOG_DBG("TOC", "window %d+%d in %lu ms", clamped, windowCount, millis() - start)` in `refreshTocWindow`, for SC-005. For the EPUB comparison, add the same line to `EpubReaderChapterSelectionActivity` locally, only for the measurement, and do not commit it (the spec excludes EPUB changes).
- [X] T034 [US1] In `docs/file-formats.md`, set the chapter count range to `1..32767 (FB2_CHAPTER_INDEX_LIMIT)`, and replace the "count is capped because this metadata is RAM-resident" sentence with the ceiling rule. Run `bin/run-tests` and `bin/run-tests --asan`, and commit `feat(fb2): remove the 256-chapter cap`.

**Checkpoint**: US1 and US2 both hold, and the 22 capped corpus books now list every chapter.

---

## Phase 5: User Story 3 — Nothing else changes for existing books (Priority: P2)

**Goal**: existing books keep their layouts and saved positions, except where the layout was built under capped boundaries. Page turns do no index I/O.

**Independent Test**: after the v4 → v5 migration, a book with ≤256 chapters keeps `sections/`, and a book with >256 loses it. `calculateProgress` performs zero opens.

### Tests for User Story 3 (write first)

- [ ] T035 [P] [US3] In `test/fb2_book/Fb2BookTest.cpp`, add three migration cases. In each, write a v4 `book.bin` (byte layout from the old `saveMetadataCache`) plus a dummy `sections/0.bin`, then `load()`:
  1. A book with 200 chapters: `sections/0.bin` still exists.
  2. A book with 300 chapters: `sections/` is gone.
  3. A corrupt **v5** file for a book with 300 chapters: `sections/` survives, because a v5 cache used the same numbering.

  Mutation: drop the version condition.
- [ ] T036 [P] [US3] In `test/fb2_book/Fb2BookTest.cpp`, add a test that after `resetOpenCounts()`, `calculateProgress(info, f)` and `getBookSize()` together perform 0 `openForReadCount()` (SC-004). The reader-side guard (T022's `chapterInfoIndex` check) is activity code with no host test; it is verified by inspection plus quickstart device step 5.
- [ ] T037 [P] [US3] In `test/xtc_fb2_readers/Fb2ReaderMathTest.cpp`, confirm the existing legacy-progress cases (4/6/8-byte payloads, `firstChapterOfTopLevel`) still pass against a v5-backed book. Nothing new should be needed beyond T014's fixture.

### Implementation for User Story 3

- [ ] T038 [US3] In `lib/Fb2/Fb2.cpp`, have `loadMetadataCache()` report the version byte it rejected (0 when there was no file or it was unreadable). In `load()`, after a successful parse: `if (rejectedVersion != 0 && rejectedVersion < FB2_CACHE_VERSION && chapterCount > FB2_OLD_CHAPTER_CAP) Storage.removeDir((cachePath + "/sections").c_str());`. Use `constexpr uint16_t FB2_OLD_CHAPTER_CAP = 256; // lowest cap any pre-v5 cache was built under; layouts past it have stale boundaries. An unreadable old book.bin reports 0 and keeps sections/: rare, accepted` (research R6). Keep `progress.bin`.
- [ ] T039 [US3] Run `bin/run-tests` and `bin/run-tests --asan`. Commit `fix(fb2): drop layouts built under the old chapter cap`.

**Checkpoint**: all stories are green on host.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T040 Run `./bin/clang-format-fix -g`, then `./bin/clang-format-fix -c`.
- [ ] T041 Build `pio run -e default` and `pio run -e x4pro`, since C3 and S3 are separate binaries. Then run `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. On macOS a green `pio check` is not a signal; read CI.
- [ ] T042 [P] Rerun the corpus tool from research R1 against the unpatched `lib/Fb2`. All 2,899 books should load and the 1,772-chapter book should list 1,772 chapters. Append the result to `specs/006-fb2-sd-chapter-lut/research.md` R1 (SC-001).
- [ ] T043 [P] Grep for leftovers: `rg "FB2_MAX_CHAPTERS|takeSections|getSections\(" lib src test docs` should return nothing outside `specs/`.
- [ ] T044 Device validation (human, gate 6): run `quickstart.md` device steps 1–5 on the X4, and record the `[MEM] Free` figures, window-refresh timings and first-open timings in `specs/006-fb2-sd-chapter-lut/research.md`. If first open is slower by more than the run-to-run spread, follow up on the R2 upgrade path by buffering `titles.tmp`.

---

## Dependencies & Execution Order

- **Setup (T001–T004)**: T001 is a human device step on current firmware and must happen before anything is flashed. T002–T004 have no dependencies and are all [P].
- **Foundational (T005–T009)**: needs nothing from Setup except that T008 is independent. It blocks every story.
- **US2 (T010–T025)**: needs Phase 2, T002 (T010), T003 (T013) and T004 (T012, T014). The tests T010–T015 run in parallel. Then T016 → T017 → T018 → T019 → T020 in sequence (all in `lib/Fb2/Fb2.{h,cpp}`). After that, T021, T022, T023 and T024 can run in parallel. T025 comes last.
- **US1 (T026–T034)**: needs US2. The tests T026–T029 run in parallel. T030 comes first; then T031→T032 (math first, then the reader) and T033 in parallel; T034 comes last.
- **US3 (T035–T039)**: needs US2 only; it can run alongside US1, but the tasks touch the same `Fb2.cpp`, so running them in sequence is simpler.
- **Polish**: after all stories.

## Parallel Examples

```text
# Setup
T002 AllocCounter live/peak   |  T003 stub open counter  |  T004 writeV5BookBin helper

# US2 tests (before any Fb2.cpp change)
T010 memory independence  |  T011 round-trip  |  T012 malformed corpus  |  T013 failed build  |  T014 Fb2MathTest fixture  |  T015 read failure

# US2 callers after T020
T021 Fb2Section  |  T022 reader chapterInfo  |  T023 chapter list  |  T024 file-formats doc

# US1 tests
T026 fb2_book ceiling  |  T027 parser ceiling  |  T028 section lockstep  |  T029 binary search
```

## Implementation Strategy

- **MVP = Phase 2 + US2**: chapter memory is flat and the cap is unchanged. It is shippable on its own: it fixes nothing visible, but it removes the RAM risk.
- **Then US1**: the visible fix. The 22 corpus books that were capped now list every chapter.
- **Then US3**: correct migration for books that exceeded the old cap. Until it lands, a book with >256 chapters that had a v4 cache can show stale layouts for chapters whose boundaries changed, so **do not merge US1 to master without US3**.
- **Device-only checks, accepted:** FR-006 (flat list memory), the positions half of FR-010 for books over 256, and the SC-004 reader guard. Each is covered in the quickstart device steps.
- **Skipped (ponytail)**: buffered temp writes (R2), chapter-list glyph prewarm (R9), code shared with EPUB. Each has a named upgrade trigger.
