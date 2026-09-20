---

description: "Task list for FB2 Reader Hardening"
---

# Tasks: FB2 Reader Hardening

**Input**: Design documents from `/specs/004-fb2-reader-hardening/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/)

**Tests**: REQUIRED for host-reachable logic (Constitution Principle V, NON-NEGOTIABLE). The
ceiling, the label derivation and the `book.bin` v4 format are all host-reachable and land with
their tests. The cover page (V1–V7) is not host-reachable — it is verified in the simulator, as
the plan's Complexity Tracking records.

**Organization**: one phase per user story, three separate commits, each individually
cherry-pickable. US2 shares no file with the others; US1 and US3 share `lib/Fb2/Fb2.h` but touch
disjoint lines of it (T006 vs T020), so neither carries the other's change.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3, mapping to the user stories in spec.md

## Path Conventions

Firmware repository: reader core in `lib/`, UI in `src/activities/`, host tests in `test/`.

---

## Phase 1: Setup

**Purpose**: capture the "before" numbers the retuned budgets will be measured against.

- [ ] T001 Run `bin/run-tests --filter 'fb2'` and record, in the scratchpad, the current pass
      state and the two allocation figures printed by
      `test/fb2_book/Fb2BookTest.cpp` `ChapterMetadataAllocatesOneTitlePerChapterAndStaysCapped`
      (measured on macOS arm64 at spec time: 8,944 bytes / 57 blocks at 40 chapters; 148,328
      bytes at the 1024 ceiling). These are the baseline T003 must be shown to move.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the one file both US1 and US3 need, changed once so the stories do not collide in
it.

**⚠️ Blocks US1 and US3. US2 (the cover page) does not depend on it.**

- [ ] T002 In `test/fb2_common/Fb2TestSupport.h`, add two deterministic generators beside
      `makeSectionTowerFb2` (whose sections always carry titles, so it cannot exercise labels):
      (a) `makeUntitledSectionsFb2(int sectionCount, int chainDepth)` — sections with no
      `<title>`, each opening with a first `<p>` of known text, plus at least one section that is
      title-less **and** text-less (for L7) and one whose first `<p>` exceeds 64 characters and
      contains multi-byte UTF-8 (for L6);
      (b) `makeShortTitleTowerFb2(int sectionCount)` — titles under the 15-character SSO
      threshold, so a budget test can separate the per-chapter struct cost from title heap
      blocks. No randomness; both must be byte-identical across runs.

**Checkpoint**: fixtures settled; US1 and US3 can proceed independently.

---

## Phase 3: User Story 1 - A large FB2 opens instead of exhausting the device (Priority: P1) 🎯 MVP

**Goal**: no real book's chapter metadata can take half the device's heap, and the chapter list
stops scaling with chapter count.

**Independent Test**: open a book whose section count exceeds 256 — it opens, every byte of text
is reachable, the chapter list scrolls first row to last, and metadata allocation is flat past
the ceiling.

### Tests for User Story 1 ⚠️ write first, watch them fail

> These run against the **current** `FB2_MAX_CHAPTERS = 1024`. T006 is what turns them green —
> that is the mutation demonstration Principle V requires, and it is why the constant change sits
> after the tests rather than in the foundational phase.

- [ ] T003 [P] [US1] In `test/fb2_book/Fb2BookTest.cpp`, retune
      `ChapterMetadataAllocatesOneTitlePerChapterAndStaysCapped` to the new ceiling: replace the
      `320 * 1024` capped-book budget with one that **fails at 1024 and passes at 256**, and
      record the newly measured figures in the test comment the way the existing comment does.
      Run it before T006 and capture the failure — a budget test that passes both before and
      after guards nothing.
- [ ] T004 [P] [US1] In `test/fb2_book/Fb2BookTest.cpp`, add a case that a `book.bin` written at
      the **old** ceiling (a valid v-current header claiming 1024 chapters) is rejected and the
      book reparsed at 256 — this is the upgrade path real SD cards take, and it must not be a
      partial read. Assert, with an `alloc_counter::CountingScope`, that the rejection allocates
      nothing unbounded.
- [ ] T005 [P] [US1] In `test/fb2_book/Fb2BookTest.cpp`, extend
      `ChapterCountIsCappedWhenParsingAHugeBook` to assert contract C6 end to end at the new
      ceiling: `getSectionCount() == 256`, and the sum of every chapter's `length` still equals
      `getBookSize()` — no text is lost when the tail collapses into its containing chapter.

### Implementation for User Story 1

- [ ] T006 [US1] In `lib/Fb2/Fb2.h`, change `FB2_MAX_CHAPTERS` `1024` → **`256`** and rewrite the
      existing `ponytail:` comment to carry the measured justification: 99,716 B at 1024 vs
      43,732 B at 256, against 222,180 B of usable heap (research.md M1–M3); upgrade path is
      issue #8's SD-resident LUT. Use "ceiling" rather than "cap" in the new wording. This is the
      only line of `Fb2.h` US1 touches — the label constant and the `titleDerived` field belong
      to US3 (T020), so the two stories stay separately cherry-pickable.
- [ ] T007 [US1] In `lib/Fb2/Fb2/Fb2MetadataParser.cpp`, call `sections.shrink_to_fit()` at the
      end of `parse()` (after the whole-file fallback). `sections` grows by doubling, so a parse
      leaves up to 2× capacity allocated; the ceiling's memory guarantee is about what a book
      *holds*, not what its parse peaked at (research.md Decision 1, "free consequences").
- [ ] T008 [US1] In `src/activities/reader/Fb2ReaderChapterSelectionActivity.h`, replace
      `std::vector<std::string> rowLabels` and `std::vector<ListItem> rowItems` with the fixed
      window from data-model.md: `static constexpr int TOC_WINDOW = 24;`,
      `std::string windowLabels[TOC_WINDOW]`, `freeink::ui::ListItem windowItems[TOC_WINDOW]`,
      `int windowStart = -1`, `int windowCount = 0`, and declare `void refreshTocWindow(int
      start)` in place of `buildRowItems()`.
- [ ] T009 [US1] In `src/activities/reader/Fb2ReaderChapterSelectionActivity.cpp`, implement
      `refreshTocWindow(start)` on the pattern of
      `EpubReaderChapterSelectionActivity::refreshTocWindow` (`EpubReaderChapterSelectionActivity.cpp:57-80`):
      clamp `start` to `[0, max(0, total - TOC_WINDOW)]`, return early when it equals
      `windowStart`, fill `windowCount = min(TOC_WINDOW, total - clamped)` rows keeping the
      existing indent rule (`level` capped at three steps, two spaces per step) and the
      `STR_UNNAMED` fallback, and set each row's `actionValue` to its **absolute** chapter index
      so `activateIndex` is unchanged. Delete `buildRowItems()` and its call in `onEnter()`.
      **Do not** copy EPUB's `prewarmFallbackText` batch or its `fcm->clearCache()` — both pay
      for SD-backed TOC entries that FB2 does not have (research.md Decision 2).
- [ ] T010 [US1] In `src/activities/reader/Fb2ReaderChapterSelectionActivity.cpp`'s
      `buildScreen`, call `refreshTocWindow(nav.top)` **after** `syncListViewport(screen, props)`
      (which is what applies follow/clamping to `nav.top`), then set `props.items = windowItems`
      and `props.itemsWindowFirst = static_cast<uint16_t>(windowStart)`. Replace the
      `rowItems.empty()` guard with `listCount() == 0`. Record the row-buffer sizes in the commit
      message: they are the evidence for SC-003, which no host test can reach.
- [ ] T011 [P] [US1] Mirror the ceiling per FR-007 — the value lives in `lib/Fb2/Fb2.h` and
      nowhere else in code. Update the **live** documentation only: `docs/file-formats.md`
      (lines ~708 and ~725, `1..1024` and "`FB2_MAX_CHAPTERS` (1024)"). Do **not** rewrite the
      landed `specs/003-fb2-nested-chapters` documents, which record what was decided then —
      add a one-line forward pointer at `specs/003-fb2-nested-chapters/research.md:173` and
      `specs/003-fb2-nested-chapters/contracts/chapter-model.md:56` saying the ceiling was later
      lowered to 256 by this feature, and leave their values intact.

**Checkpoint**: US1 is complete and shippable on its own. One commit:
`perf: bound FB2 chapter metadata and window the chapter list`.

---

## Phase 4: User Story 2 - An FB2 book opens on its cover (Priority: P2)

**Goal**: the cover the sleep screen already draws also opens the book, without moving a single
chapter index, page number or cached page.

**Independent Test**: open an FB2 with a cover at the beginning → the cover shows; one forward
turn → first page of text; reopen a book saved mid-chapter → it lands on that text, not the
cover.

**No host tests**: V1–V7 are reader state and rendering, which the host suites cannot reach.
They are verified in the simulator (T017) rather than faked with a stub that would pass on host
and be dead on device (Principle V).

### Implementation for User Story 2

- [ ] T012 [US2] In `src/activities/reader/Fb2ReaderActivity.h`, add `bool onCoverPage = false;`
      and `std::string coverBmpPath;` per data-model.md's "Reader cover state". Neither is
      persisted.
- [ ] T013 [US2] In `src/activities/reader/Fb2ReaderActivity.cpp`'s `loadBook()`, after
      `loadProgress()`, resolve the cover once: call `fb2->generateCoverBmp()` (it short-circuits
      when `cover.bmp` already exists, `lib/Fb2/Fb2.cpp`) and store `fb2->getCoverBmpPath()` in
      `coverBmpPath` on success, leaving it empty otherwise. Set
      `onCoverPage = !coverBmpPath.empty() && currentSectionIndex == 0 && nextPageNumber == 0`
      — V1 and V3 in one expression.
- [ ] T014 [US2] In `src/activities/reader/Fb2ReaderActivity.cpp`'s `renderBook()`, after the
      section is loaded and `section->currentPage` is clamped, and **before** `loadPage()`, take
      the cover branch when `onCoverPage`: `renderer.clearScreen()`, open `coverBmpPath` through
      `Storage.openFileForRead`, construct a `Bitmap`, and on `parseHeaders() == BmpReaderError::Ok`
      draw it with `sleepimage::calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(),
      renderer.getScreenWidth(), renderer.getScreenHeight(), /*crop=*/false)` +
      `renderer.drawBitmap(...)`, then `renderer.displayBuffer()` and return. On **any** failure
      — open, parse or placement — clear `onCoverPage` and fall through to the normal page render
      (V2): no blank page, no error screen. Keep the existing `saveProgress(currentSectionIndex,
      section->currentPage, section->pageCount)` call on the cover path so the stored position
      stays chapter 0 / page 0 (V6). Loading the section first is deliberate: the reader menu and
      `bookProgressPercent()` read `section->pageCount` (V5).
- [ ] T015 [US2] In `src/activities/reader/Fb2ReaderActivity.cpp`'s `pageTurn()`, implement V4:
      when `onCoverPage`, a forward turn clears it and returns true (chapter 0 / page 0 renders),
      and a backward turn returns **false** (nothing precedes the cover); when not on the cover,
      a backward turn at `currentSectionIndex == 0 && section->currentPage == 0` with a non-empty
      `coverBmpPath` sets `onCoverPage` and returns true, instead of today's `return false`.
- [ ] T016 [US2] In `src/activities/reader/Fb2ReaderActivity.cpp`, clear `onCoverPage` on every
      other route out of the cover (V7): `skipPages()`, the `SELECT_CHAPTER` result handler,
      `jumpToPercent()`, and the re-pagination path in `onReaderMenuConfirm` that resets
      `section`. One assignment each; a missed one leaves the cover painted over a jump target.
- [ ] T017 [US2] Verify V1–V7 in the simulator per [quickstart.md](quickstart.md) §4 steps 1, 2,
      5 and 6. Cover all three failure shapes SC-008 names, not just the easy one: `cover.bmp`
      **deleted**, **truncated** mid-file, and one whose header **lies about its dimensions**.
      `Bitmap::parseHeaders` already rejects all three (`test/gfx_renderer/BitmapTest.cpp`); what
      is being verified here is the reader's response — falls through to the first page of text,
      no blank page, no error screen (FR-010, V2). Record what was observed in the commit
      message: it is the only evidence this story gets.

**Checkpoint**: US2 is complete and shippable on its own. One commit:
`feat: open FB2 books on their cover`.

---

## Phase 5: User Story 3 - The chapter list reads as titles, not "Unnamed" (Priority: P3)

**Goal**: an untitled section is labelled with its own first line, and the marker that says so
survives the cache.

**Independent Test**: open the chapter list of an FB2 with untitled sections — each row with
text of its own shows that text, rows with neither title nor text keep the placeholder, and
selecting any row opens the chapter it names.

### Tests for User Story 3 ⚠️ write first, watch them fail

- [ ] T018 [P] [US3] In `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`, cover L1–L8 from
      [contracts/reader-behaviour.md](contracts/reader-behaviour.md) with the T002 generators and
      the existing fixtures: a real `<title>` wins and `titleDerived` is 0 (L1); an untitled
      section takes its own first `<p>` with `titleDerived` 1 (L2); a child section's text never
      labels its parent (L3); only the first `<p>` contributes (L4); whitespace is stripped and
      collapsed (L5); a label is cut to 64 **characters** on a UTF-8 boundary, never mid-character
      (L6); a section with neither title nor printable text stores an empty title (L7); and no
      `fileOffset`, `length`, `level` or chapter index changes versus the same file parsed before
      the feature (L8).
- [ ] T019 [P] [US3] In `test/fb2_book/Fb2BookTest.cpp`, cover the v4 format from
      [contracts/file-formats.md](contracts/file-formats.md): a derived label and its flag
      survive the write/read round trip; a valid **v3** cache is rejected and the book reparsed
      (no partial read, nothing unbounded allocated); `flags` with any bit outside `0x01` is
      rejected; and `flags & 0x01` with an empty title is rejected. Reuse the byte-editing
      pattern of `ChapterLevelOutOfSequenceIsRejected` and run the suite under `--asan`.

### Implementation for User Story 3

- [ ] T020 [US3] In `lib/Fb2/Fb2.h`, add the two things US3 owns — and nothing else, so the
      commit stays separable from US1's T006: `static constexpr uint16_t FB2_MAX_LABEL_CHARS =
      64;` (the character cap for a **derived** label only, never for a real `<title>`), and
      `uint8_t titleDerived = 0;` in `Fb2::SectionInfo` **immediately after `level`**, so it packs
      into the existing padding — verified on riscv32, `sizeof(SectionInfo)` stays 36 B
      (research.md M2).
- [ ] T021 [US3] In `lib/Fb2/Fb2/Fb2MetadataParser.h/.cpp`, derive the label. Add a context for
      "inside the first `<p>` of a section that has no title yet", gated on the innermost open
      section (`openSections.back()`) being a chapter, so L3 holds by construction. Append into
      that chapter's existing `title` field — it is empty by definition here — set
      `titleDerived = 1`, stop at the closing `</p>` (L4), and stop appending once
      `FB2_MAX_LABEL_CHARS` characters are held. Collapse internal whitespace runs and strip the
      ends (L5), then cut with `utf8TruncateChars` from `lib/Utf8/Utf8.h` (L6). If a real
      `<title>` arrives afterwards, clear the derived text, reset `titleDerived` to 0 and store
      the title uncapped (L1). No new buffer member: reusing the `title` field is what keeps the
      per-chapter cost at the measured 36 B.
- [ ] T022 [US3] In `lib/Fb2/Fb2.cpp`, take `book.bin` to v4: bump `FB2_CACHE_VERSION` 3 → 4
      (line 16), write and read the per-chapter `flags` byte after `level`, grow
      `FB2_CACHE_MIN_SECTION_ENTRY` by one byte so the pre-`reserve()` size check stays honest,
      and validate before use — `flags & ~0x01` must be 0, and `flags & 0x01` implies a non-empty
      title. Any failure keeps the existing reject-and-reparse path (FR-021, Principle VI).
- [ ] T023 [P] [US3] In `lib/I18n/translations/english.yaml`, add
      `STR_DERIVED_CHAPTER_LABEL_FORMAT: "“%s”"` following the existing `*_FORMAT` naming
      convention (e.g. `STR_DEVICE_FROM_FORMAT`). English is the reference; the other 33
      languages fall back to it until translated, so no other YAML file is edited here.
- [ ] T024 [US3] In `src/activities/reader/Fb2ReaderChapterSelectionActivity.cpp`'s
      `refreshTocWindow`, render a row whose `titleDerived` is set through
      `tr(STR_DERIVED_CHAPTER_LABEL_FORMAT)` with `snprintf` into a stack buffer, so a derived
      label reads as the book's words rather than as a title the book supplied (FR-016). Real
      titles and the `STR_UNNAMED` placeholder are unchanged. **Depends on T009** (the window) —
      the only cross-story dependency in the feature.
- [ ] T025 [P] [US3] In `docs/file-formats.md`, rewrite the `book.bin` section (lines ~700-730)
      for version 4: the `u8 flags` field with bit 0 documented, the two new validation rules,
      the derived-label rule and its 64-character cap, and a line saying real titles are stored
      as the book supplies them. Add the v3 → v4 note to the version history the way v2 → v3 is
      recorded.
- [ ] T026 [US3] Verify the chapter list in the simulator per [quickstart.md](quickstart.md) §4
      steps 3 and 4 — the steps T017 deliberately leaves to this story. On a nested anthology:
      zero rows show the placeholder where the section has text of its own (SC-007), derived rows
      are distinguishable from real titles (FR-016), indentation by nesting level is unchanged,
      the list scrolls first row to last without stutter or missing rows, and selecting a row
      opens the chapter it names (FR-019, SC-004, FR-022). Record what was observed in the commit
      message.

**Checkpoint**: US3 is complete. One commit: `feat: label untitled FB2 chapters from their first
line`.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T027 [P] Add the `ponytail:` comments the design owes, each naming its ceiling and upgrade
      path: at the cover branch in `Fb2ReaderActivity::renderBook` (black-and-white only; the
      sleep screen's three-pass grayscale pipeline is the upgrade if a dithered cover reads
      poorly), and at `refreshTocWindow` in `Fb2ReaderChapterSelectionActivity.cpp` (no fallback
      glyph prewarm; EPUB's batch is the upgrade if CJK lists repaint slowly).
- [ ] T028 Run the merge gates in order and fix what they find: `./bin/clang-format-fix -g`,
      `bin/run-tests`, `bin/run-tests --asan`, `pio run -e default`,
      `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`.
- [ ] T029 [P] Build one S3 board — `pio run -e sticky` — since a change can build on C3 and
      fail on S3; CI builds all five environments.
- [ ] T030 Hand the device checks to the human tester per [quickstart.md](quickstart.md) §5 and
      §7, and include the **SC-003 heap check** that no host test can reach: free heap after
      opening a 4-chapter FB2 and after opening one at the ceiling, which must differ only by
      chapter metadata and not by chapter-list buffers. Also: the cover page in all four
      orientations, the dithered cover's appearance, and that a position saved before the change
      reopens on the same text with its page cache intact.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: no dependencies.
- **Foundational (T002)**: fixtures. Blocks US1 and US3. **Does not block US2.**
- **US1 (T003–T011)**: after T002. Tests T003–T005 run against the *old* ceiling; T006 flips them.
- **US2 (T012–T017)**: independent of everything above — it touches only
  `Fb2ReaderActivity.{h,cpp}` and can start immediately.
- **US3 (T018–T026)**: after T002. T024 also depends on T009 (US1's window), the only cross-story
  dependency in the feature.
- **Polish (T027–T030)**: after the stories you intend to ship.

### Within US1

T003–T005 (tests, failing) → **T006 (flips them green)** → T007 → T008 → T009 → T010 (same file,
strictly ordered). T011 is parallel with all of them.

### Within US3

T018–T019 (tests) → T020 (header) → T021 (parser) → T022 (format) → T024 (display, needs T009) →
T026 (simulator). T023 and T025 are parallel with all of it.

### Parallel Opportunities

- T003, T004, T005 — three independent test cases, no ordering between them.
- T011, T023, T025 — documentation and translations, no code dependency.
- US2 in full, alongside US1 or US3, by a second person: disjoint files.

---

## Implementation Strategy

### MVP (User Story 1 only)

T001 → T002 → T003–T011. Ships the measured memory fix: worst real book's chapter metadata from
99,716 B (44.9% of usable heap) to 43,732 B (19.7%), and a chapter list whose cost no longer
scales with chapter count. Stop here and validate: `bin/run-tests --filter 'fb2'`, then the
simulator's chapter list on a nested anthology.

### Incremental Delivery

Each story is one commit and one cherry-pick, touching disjoint lines even where it shares a
file:

1. US1 → `perf: bound FB2 chapter metadata and window the chapter list` (`Fb2.h`: the ceiling)
2. US2 → `feat: open FB2 books on their cover` (no shared files at all)
3. US3 → `feat: label untitled FB2 chapters from their first line` (`Fb2.h`: the label constant
   and the flag)

Nothing in US2 or US3 breaks US1, and each can be dropped without touching the others — except
T024, which needs US1's window to exist.

---

## Notes

- **The escape hatch**: FR-016 (a derived label must be visually distinguishable) is what forces
  the `flags` byte, which forces the v4 bump, which forces one metadata reparse per book.
  Dropping FR-016 deletes T019's flag cases, T022, T023, T024's format string and the version
  bump — US3 becomes a parser change alone. That is the single decision to revisit if the
  rebuild is judged not worth it.
- **What the measurement leaves open**: 256 halves the worst case but degrades 22 books of 2,899
  (0.76%), whose tails collapse into one chapter — a median 31.5% of the book's text, worst
  77.2%. That is a pagination cost, and the strongest argument for issue #8's SD-resident LUT.
  It is out of scope here and stays open.
- **Two criteria have no host test by design**: SC-003 (chapter-list memory) and V1–V7 (the cover
  page) are activity code. They are verified by inspection and on device (T010, T017, T026, T030)
  rather than by a host guard that would pass on host and be dead on device (Principle V).
- Commit after each story, not each task. Never on `master` (Constitution VII).
- No AI attribution or AI co-authors in any commit message.
