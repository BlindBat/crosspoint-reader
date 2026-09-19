---

description: "Task list for FB2 Nested Chapter Navigation"
---

# Tasks: FB2 Nested Chapter Navigation

**Input**: Design documents from `/specs/003-fb2-nested-chapters/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/chapter-model.md](contracts/chapter-model.md),
[contracts/file-formats.md](contracts/file-formats.md)

**Tests**: REQUIRED (Constitution Principle V, non-negotiable). Every task below touches
parser, format, or cache logic that the host program reaches, so each change lands with
gtest coverage, deterministic fixtures, and corpus entries for untrusted input. The four
tests that pin today's behaviour must be shown **failing** against unmodified production
code before any production edit (T004).

**Organization**: grouped by user story. No new test suite or CMake entry is needed — all
work lands in the existing `fb2_metadata_parser`, `fb2_section_parser`, `fb2_book`,
`fb2_section_cache`, `xtc_fb2_readers` and `corpus` suites.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 from [spec.md](spec.md)

## Path Conventions

Repository root. Production code in `lib/Fb2/` and `src/activities/reader/`; host tests in
`test/<suite>/`; FB2 fixtures in `test/fb2/`.

---

## Phase 1: Setup (Fixtures & Test Helpers)

**Purpose**: the deterministic inputs every later phase asserts against. No production code.

- [ ] T001 [P] Add fixture `test/fb2/nested-deep.fb2`: one reading body, three nesting levels (book → part → chapter → scene), a parent section whose direct content includes a `<poem><title><p>Poem Title</p></title>` (must NOT become the section's title per contract C4), a child section title, and unique marker words per level (`levelzeroword`, `levelone`, `leveltwo`, `levelthree`) so extent tests can assert containment precisely
- [ ] T002 [P] Add fixture `test/fb2/wrapper-only.fb2`: a titled parent section containing one untitled `<section>` that has no direct content at all (pure wrapper), plus a titled sibling with content — the shape that exercises contract C8 (empty chapter still gets one page)
- [ ] T003 [P] Add fixture `test/fb2/trailing-parent-text.fb2`: a parent with `<p>` content before AND after its single child section, each paragraph carrying a unique marker word, so the documented reordering ceiling (research.md Decision 1) is pinned rather than accidental
- [ ] T004 [P] Add a committed generator helper `makeSectionTowerFb2(int sectionCount, int depth)` to `test/fb2_common/Fb2TestSupport.h` that writes a deterministic FB2 file (no randomness) with the requested number of sections into a `TempDir`, for the chapter-cap and hostile-nesting tests

**Checkpoint**: fixtures exist and are readable by `fb2test::fixturePath` / `TempDir`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: encode the new contract in the pin tests (red first), then add the one shared
data field every story needs.

**⚠️ CRITICAL**: T005 must be demonstrated failing before any production edit; no user
story work begins until T006 compiles.

- [ ] T005 Rewrite the four tests that pin the current top-level-only model to the contract in `contracts/chapter-model.md`, run them against unmodified production code, and record the failures in the task notes as the mutation evidence Principle V requires: `NestedSectionsOnlyTopLevelOnesAreTracked` in `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp:100` (→ 3 chapters, levels `0,1,0`, titles `Part One The Beginning` / `Inner Chapter` / `Part Two`), and `NestedSectionContentStaysInsideTargetSectionZero`, `NestedSectionsDoNotSkewTargetSectionIndexing`, `NestedSectionsDoNotExtendTheTopLevelIndexRange` in `test/fb2_section_parser/Fb2SectionParserTest.cpp:434,448,457` (→ chapter 0 excludes `innerword`, chapter 1 is `Inner Chapter`, index 3 renders nothing)
- [ ] T006 In `lib/Fb2/Fb2.h`: add `uint8_t level` to `Fb2::SectionInfo` (0 = direct child of `<body>`) and `uint8_t level` to `Fb2::TocEntry`; add `static constexpr uint16_t FB2_MAX_CHAPTERS = 1024` where both parsers and the cache reader can see it; declare `int firstChapterOfTopLevel(int ordinal) const`

**Checkpoint**: pins are red for the right reasons; the shared field exists.

---

## Phase 3: User Story 1 - Jump straight to a story inside a collection (Priority: P1) 🎯 MVP

**Goal**: every `<section>` of a reading body becomes a numbered, openable chapter whose
text is its own direct content.

**Independent Test**: open the reference anthology, open the chapter list, see 66 entries
including the 62 story titles, select one, land on its first page.

### Tests for User Story 1 (write first, must fail) ⚠️

- [ ] T007 [P] [US1] In `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`: numbering and level tests over `nested-deep.fb2` — chapters appear in document-start order at every depth (contract C2), `level` is the enclosing-`<section>` count (C5), and the first chapter's level is 0
- [ ] T008 [P] [US1] In `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`: title-scoping tests (C4) — a chapter's title comes only from a `<title>` that is a direct child of its own `<section>`; the `<poem><title>` in `nested-deep.fb2` and a child section's title never leak into the parent's title; a section with no direct title yields an empty title
- [ ] T009 [P] [US1] In `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`: auxiliary bodies stay excluded (C1) — re-assert `notes-body.fb2` yields exactly the reading-body chapters after the numbering change, and that a second unnamed `<body>` still contributes chapters
- [ ] T010 [P] [US1] In `test/fb2_section_parser/Fb2SectionParserTest.cpp`: extent tests (C3) over `nested-deep.fb2` and `trailing-parent-text.fb2` — a chapter contains its own marker words and none of its children's; the trailing parent paragraph reads with the parent (the documented ceiling), not dropped
- [ ] T011 [US1] In `test/fb2_section_parser/Fb2SectionParserTest.cpp`: the parity + completeness property from `contracts/chapter-model.md` over every fixture in `test/fb2/` — for each `i` reported by `Fb2MetadataParser` the render parser's text includes that chapter's own words and excludes child chapters'; target `getSectionCount()` renders nothing; the concatenation of chapters `0..count-1` contains every reading-body word exactly once. Mutation-verify it (Principle V): with `Fb2SectionParser`'s counter reverted to top-level-only while `Fb2MetadataParser` keeps the new numbering, this test MUST fail — it is the only guard against the chapter list and the renderer disagreeing
- [ ] T012 [P] [US1] In `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: a pure-wrapper chapter from `wrapper-only.fb2` builds a section file with `pageCount == 1` (contract C8), and `loadPage(0)` returns a page rather than `nullptr`. Also assert per-chapter page counts for `nested-deep.fb2` (FR-006): counts differ between chapters and no chapter's count equals the sum over all chapters — i.e. a chapter is paginated over its own content, not the book's
- [ ] T013 [P] [US1] In `test/fb2_book/Fb2BookTest.cpp`: `book.bin` v3 round-trip — titles, `fileOffset`, own `length` and `level` survive save/load identically; a v2 file (and a file with a `level` sequence that jumps by more than 1) is rejected and the source is reparsed; `getTocCount() == getSectionCount()` and both index mappings are the identity. The existing flat-book expectations in this suite (`basic.fb2`, `no-sections.fb2`, `long.fb2`) are the SC-006 regression guard: they MUST keep passing unedited — adjusting them to make the new code pass silently voids "flat books behave identically". Note the v3 layout deletes the TOC list, so the existing `Fb2BookTest.cpp:228` "corrupt TOC count is rejected not allocated" test loses its subject — replace it with the equivalent guard on the new per-chapter `level` validation rather than deleting the coverage
- [ ] T014 [P] [US1] In `test/fb2_book/Fb2BookTest.cpp` using the T004 generator: a file with more than `FB2_MAX_CHAPTERS` sections yields exactly 1024 chapters, loses no text (contract C6), and allocates nothing unbounded under ASan
- [ ] T015 [P] [US1] In `test/corpus/`: malformed-nesting entries — unbalanced `<section>` tags, 64-level-deep nesting, a `book.bin` claiming 65535 chapters, a `book.bin` truncated mid-chapter-record — each producing a bounded, deterministic outcome with no sanitizer finding (FR-013, FR-014)

### Implementation for User Story 1

- [ ] T016 [US1] In `lib/Fb2/Fb2/Fb2MetadataParser.h/.cpp`: replace the `sectionDepth == 1` filter (`Fb2MetadataParser.cpp:97-103`) with a section stack — `std::vector` of `{entryIndex, startOffset, childBytes}` with `reserve(8)` before any `push_back` (Principle II). Push a chapter entry at each `<section>` start (document order) recording `level`; on its end tag set `length = (end - start) - childBytes` per contract C7 and add the full span to the parent's `childBytes`. Track element depth so `<title>` is attributed only to the section it is a direct child of (C4). Stop creating chapter boundaries once `FB2_MAX_CHAPTERS` is reached (C6). Keep the existing `hasNameAttribute` body rule untouched (C1)
- [ ] T017 [US1] In `lib/Fb2/Fb2/Fb2MetadataParser.h/.cpp`: delete the `tocEntries` vector and `getTocEntries()`; the whole-file fallback at the end of `parse()` keeps producing exactly one chapter with `fileOffset == 0` and `level == 0` (contract C9)
- [ ] T018 [US1] In `lib/Fb2/Fb2/Fb2SectionParser.h/.cpp`: number every section inside a reading body in document-start order, counting **before** any content suppression so a nested chapter's own subtree still advances the counter (research.md Decision 2 — do NOT reuse `skipUntilDepth`, whose early return at `Fb2SectionParser.cpp:93-99` would stop counting). Add a separate suppression depth that gates the content handlers (`startElement` content branches, `characterData`, `endElement` flush) while a child chapter's subtree is open, and clear it on the matching close. Apply `FB2_MAX_CHAPTERS` with the same comparison at the same point as T016 so the two numberings cannot diverge — add `#include "Fb2.h"` to `Fb2SectionParser.cpp` to reach the constant (its header pulls in only Epub/expat headers today); do NOT declare a second copy of it. Preserve the `targetSectionIndex < 0` whole-body fallback
- [ ] T019 [US1] In `lib/Fb2/Fb2/Fb2SectionParser.cpp`: in `parseAndBuildPages()`, if the parse completed but no page was ever handed to `completePageFn`, emit one empty page (contract C8) — this failure mode is new, and `Fb2Section::loadPage` returns `nullptr` for every page when `pageCount == 0` (`Fb2Section.cpp:218-221`)
- [ ] T020 [US1] In `lib/Fb2/Fb2.h/.cpp`: delete the `tocEntries` member; implement `getTocCount()`, `getTocEntry(i)` (title + `level`), `getTocIndexForSectionIndex(i)` and `getSectionIndexForTocIndex(i)` as projections/identities over `sections`, keeping the existing out-of-range empty sentinels so `Fb2ReaderChapterSelectionActivity` and `Fb2ReaderActivity.cpp:459` need no change
- [ ] T021 [US1] In `lib/Fb2/Fb2.cpp`: bump `FB2_CACHE_VERSION` 2 → 3 (`lib/Fb2/Fb2.cpp:13`) and implement the v3 layout from `contracts/file-formats.md` — write/read `level` per chapter, drop the trailing TOC list and `FB2_CACHE_MIN_TOC_ENTRY`, set `FB2_CACHE_MIN_SECTION_ENTRY = 4 + 4 + 4 + 1`, and validate before any `reserve()`: `1 <= chapterCount <= FB2_MAX_CHAPTERS`, `chapterCount * FB2_CACHE_MIN_SECTION_ENTRY <= bytes remaining`, every string `<= FB2_CACHE_MAX_STRING (4096)`, first `level == 0` and `level <= previous + 1`. Any failure keeps the existing reject-and-reparse path (FR-011, Principle VI)
- [ ] T022 [US1] In `lib/Fb2/Fb2/Fb2Section.cpp`: bump `FB2_SECTION_FILE_VERSION` 4 → 5 (`Fb2Section.cpp:20`) with a comment stating the reason — the layout is unchanged but the file's name (the chapter index) changed meaning, so a stale v4 `sections/1.bin` would silently render the wrong chapter
- [ ] T023 [US1] Add the two `ponytail:` ceiling comments the design owes: at the `FB2_MAX_CHAPTERS` check in `Fb2MetadataParser.cpp` (RAM-resident chapter metadata; upgrade path is an SD-resident seekable LUT as `BookMetadataCache` does for EPUB) and at the child-subtree suppression in `Fb2SectionParser.cpp` (parent text after a child reads with the parent, ahead of the children)

**Checkpoint**: `bin/run-tests --filter fb2` green; nested stories are numbered, listed and
openable with correct text. This alone is a shippable MVP.

---

## Phase 4: User Story 2 - See which part a story belongs to (Priority: P2)

**Goal**: the chapter list conveys hierarchy, so 66 entries stay legible.

**Independent Test**: open the reference book's chapter list — the four part entries are
un-indented, the stories under each are indented one step, in document order.

### Tests for User Story 2 (write first, must fail) ⚠️

- [ ] T024 [P] [US2] In `test/fb2_book/Fb2BookTest.cpp`: `getTocEntry(i).level` equals `getSectionInfo(i).level` for every fixture, survives a `book.bin` v3 round-trip, and an untitled chapter still reports an empty title so the UI can substitute `tr(STR_UNNAMED)` (FR-009)

### Implementation for User Story 2

- [ ] T025 [US2] In `src/activities/reader/Fb2ReaderChapterSelectionActivity.cpp` (`buildRowItems`): prefix each row label with `min(level, 3) * 2` spaces, mirroring the EPUB list's `(level - 1) * 2` indent at `src/activities/reader/EpubReaderChapterSelectionActivity.cpp:65` (EPUB TOC levels are 1-based, FB2 levels are 0-based). The cap at 3 is what keeps a deeply nested title from being pushed off the row (spec edge case "Deeply nested sections"). Keep building `rowLabels` once in `onEnter()` and keep the existing `tr(STR_UNNAMED)` substitution; add no new user-facing string and no new list component
- [ ] T026 [US2] Verify in the simulator that the current chapter's row is still pre-selected when the list opens from inside a nested story (`Fb2ReaderActivity.cpp:459` → identity mapping from T020), and that deep indents degrade gracefully rather than pushing titles off the row in all four orientations

**Checkpoint**: US1 + US2 both work; the list reads as a hierarchy.

---

## Phase 5: User Story 3 - Progress and page counts that reflect the story being read (Priority: P3)

**Goal**: page counts, progress percentage and resumed positions refer to the chapter the
reader is actually in, including for positions saved by the previous firmware.

**Independent Test**: open a nested story — the page counter covers only that story; page
past its end into the next story; progress rises in small steps; a position saved before
the update resumes in the same part.

### Tests for User Story 3 (write first, must fail) ⚠️

- [ ] T027 [P] [US3] In `test/fb2_book/Fb2BookTest.cpp`: chapter lengths partition the reading body (contract C7) — `sum(length)` equals the body's section bytes, `getCumulativeSectionSize` is strictly non-decreasing, `calculateProgress(0, 0.0f) == 0.0f` and `calculateProgress(count-1, 1.0f) == 1.0f` for `nested-deep.fb2` and `trailing-parent-text.fb2` (FR-007)
- [ ] T028 [P] [US3] In `test/fb2_book/Fb2BookTest.cpp`: `firstChapterOfTopLevel(n)` returns the index of the (n+1)-th `level == 0` chapter, is the identity for a flat book, and clamps to the last chapter for an out-of-range ordinal
- [ ] T029 [P] [US3] In `test/xtc_fb2_readers/Fb2ReaderMathTest.cpp`: `progress.bin` v2 — an 8-byte payload with marker `0xFB02` decodes as a chapter index with page and page count; an 8-byte payload with any other marker is invalid; 6-byte and 4-byte payloads decode with `legacyOrdinal` set and page forced to 0; any other size stays invalid; encoding always emits the 8-byte form
- [ ] T030 [P] [US3] In `test/xtc_fb2_readers/XtcReaderMathTest.cpp` and `test/txt_reader/TxtReaderTest.cpp`: re-assert the XTC and TXT progress payloads are untouched (the 8-byte form is FB2-local, plan post-design re-check)

### Implementation for User Story 3

- [ ] T031 [US3] In `src/activities/reader/Fb2ReaderMath.h/.cpp`: extend `fb2_reader::Progress` with `bool legacyOrdinal`, accept payload size 8 with a trailing `u16 marker == 0xFB02` (invalid on any other marker), keep sizes 4 and 6 valid but flagged legacy with `page = 0`, and add the encode side so writes always emit the 8-byte form (`decodeProgress` is at `Fb2ReaderMath.cpp:60-71`)
- [ ] T032 [US3] In `lib/Fb2/Fb2.cpp`: implement `firstChapterOfTopLevel(int ordinal)` — index of the (n+1)-th chapter with `level == 0`, clamped to the last chapter, 0 when there are none
- [ ] T033 [US3] In `src/activities/reader/Fb2ReaderActivity.cpp`: widen `loadProgress()`'s read buffer to 8 bytes (`Fb2ReaderActivity.cpp:50-68`) and, when `progress.legacyOrdinal` is set, resume at `fb2->firstChapterOfTopLevel(progress.sectionIndex)` with page 0 and no cached page count (do not `markSaved` the legacy values, or the remapped position is never written); keep the existing bounds clamping at `Fb2ReaderActivity.cpp:333-335`. **Also replace the hand-rolled byte packing in `saveProgress` (`Fb2ReaderActivity.cpp:71-79`, currently `uint8_t data[6]` filled inline) with the T031 encoder and an 8-byte buffer** — without this, reads accept 8 bytes while writes still emit 6 and no book ever migrates. `ReaderProgressGuard::save` takes `(data, len)` and is size-agnostic, so it needs no change and XTC/TXT stay on their own payloads

**Checkpoint**: all three stories functional; progress and resumed positions are meaningful.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T034 [P] Update `docs/file-formats.md`: the FB2 `book.bin` section (around line 691) for v3 including the `level` field and the removed TOC list, the FB2 section-file version (4 → 5), and the progress-payload table at line 651 for the 8-byte FB2 form
- [ ] T035 Run `./bin/clang-format-fix -g` (the only sanctioned entry point) and fix any 120-column violations
- [ ] T036 Run `bin/run-tests` (plain) and `bin/run-tests --asan` — the full program must be green in both, with `ASAN_OPTIONS=detect_leaks=1`
- [ ] T037 Run `pio run -e default` (ESP32-C3) and `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
- [ ] T038 Walk the simulator scenario in [quickstart.md](quickstart.md) §4 with the reference book on the SD card: 66 chapter entries with one indent step, open "Ракетное лето" directly, page across two chapter boundaries in both directions, watch the percentage step by a few percent, confirm the 14 `name="notes"` sections are absent
- [ ] T039 Run the legacy-position check in [quickstart.md](quickstart.md) §5 once against a cache built by the pre-change firmware: resume lands at the start of the same part, and the next save migrates the file to 8 bytes
- [ ] T040 Re-measure the spec's numbers with the script in [quickstart.md](quickstart.md) §6 and confirm SC-001/SC-003/SC-005 (66 chapters, levels `[0,1]`, largest section 67,209 bytes) still describe the shipped behaviour
- [ ] T041 [P] Cover FR-015 in `test/fb2_book/Fb2BookTest.cpp` using the `AllocCounter` scope pattern the suite already uses (`Fb2BookTest.cpp:192-199`; the suite already compiles `test/support/AllocCounter.cpp`, so no CMake change): (a) for a flat fixture (`basic.fb2`), bytes allocated by `Fb2::load()` are **no worse than** the pre-change baseline — deleting `tocEntries` removes one `std::string` per chapter, so this must improve, and it is the measurement backing the plan's RAM claim (Principle IV); (b) for the T004 section-tower file, allocated bytes stay bounded at `FB2_MAX_CHAPTERS` chapters and do not grow with the file's section count beyond the cap. Record both measured baselines in the test comment, as the suite's other budgets do. Runnable as soon as T021 lands

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: no dependencies; all four tasks are parallel
- **Phase 2 (Foundational)**: T005 needs Phase 1 fixtures; T006 needs nothing but blocks all production work
- **Phase 3 (US1)**: needs Phase 2. This is the bulk of the feature and the MVP
- **Phase 4 (US2)**: needs T006 (the `level` field) and T020 (the TOC projection); otherwise independent of US1's parser internals
- **Phase 5 (US3)**: needs T016's own-length computation (C7) for T027 and T006's `level` for T032; the `progress.bin` work (T029–T031, T033) is independent of US1/US2 and can run in parallel with Phase 4
- **Phase 6 (Polish)**: needs every shipped story, except T041 (FR-015 allocation guard), which only needs T021 and can run alongside Phase 4/5

### Within User Story 1

T007–T015 (tests, red) → T016/T017 (metadata parser) → T018/T019 (render parser) →
T020/T021 (Fb2 + cache v3) → T022 (section version) → T023 (ceiling comments). T016 and
T018 must land together conceptually: the parity test T011 fails until both implement the
same numbering.

### Parallel Opportunities

- Phase 1: T001, T002, T003, T004 — four different files
- Phase 3 tests: T007, T008, T009 (one suite, distinct tests), T010, T012, T013, T014, T015 — different suites
- Phase 5: the `progress.bin` chain (T029 → T031 → T033) runs in parallel with Phase 4's UI work
- Polish: T034 is independent of T035–T040

### Parallel Example: User Story 1 tests

```bash
# All red-first tests for US1, different suites, run together:
bin/run-tests --filter fb2_metadata_parser   # T007, T008, T009
bin/run-tests --filter fb2_section_parser    # T010, T011
bin/run-tests --filter fb2_section_cache     # T012
bin/run-tests --filter fb2_book              # T013, T014
```

---

## Implementation Strategy

### MVP (User Story 1 only)

1. Phase 1 fixtures → Phase 2 pins red → Phase 3.
2. **STOP and VALIDATE**: `bin/run-tests --filter fb2` green plain and `--asan`; open the
   reference book in the simulator and confirm the 62 stories are listed and openable.
3. This is shippable on its own: the reported defect is fixed. The list is flat-looking
   (US2 pending) and progress still steps coarsely at the top level (US3 pending), but no
   text is wrong and nothing is unreachable.

### Incremental delivery

1. US1 → chapters reachable (MVP, fixes the report).
2. US2 → one label change, the list reads as a hierarchy.
3. US3 → per-chapter page counts and migrated positions.
4. Polish → docs, gates, simulator and device verification.

### Commit granularity (Constitution VII)

One logical change per commit, cherry-pickable onto a branch cut from `origin/develop`:
(a) fixtures + red pins, (b) metadata parser model, (c) render parser extent, (d) cache v3
+ version bumps, (e) chapter-list indent, (f) progress migration, (g) docs. Test suites and
fixtures stay fork-only and must not be bundled into an upstream PR branch.

---

## Notes

- `[P]` = different files, no dependency on an incomplete task
- The merge gates (`bin/run-tests`, `--asan`, `pio run -e default`, `pio check`) are
  T036–T037 and are **not** replaced by any autocommit
- Verify each test fails before implementing it (Principle V); T005 records that evidence
- No new heap allocation is introduced outside the parser's `reserve()`d section stack; no
  bare `new` anywhere (Principle II)
