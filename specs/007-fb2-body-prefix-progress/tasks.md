---

description: "Task list for FB2 body-level front matter progress weight"
---

# Tasks: FB2 Body-Level Front Matter Carries Progress Weight

**Input**: Design documents from `/specs/007-fb2-body-prefix-progress/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/chapter-model.md](contracts/chapter-model.md)

**Tests**: REQUIRED (Constitution Principle V). Every task below touches host-reachable
reader-core logic, so each behavioural change lands with a host gtest pin that is shown to
fail against the unfixed parser.

**Organization**: grouped by user story. US1 and US2 share one production edit — US1 is the
weight that must change, US2 is everything that must not — so US2's tasks are its
regression proofs rather than a second implementation.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: US1 / US2 / US3 from [spec.md](spec.md)

## Path Conventions

Production code in `lib/Fb2/`; host tests in `test/`; fixtures in `test/fb2/`. Paths are
repository-relative.

---

## Phase 1: Setup (Baseline)

**Purpose**: capture the "before" state that FR-003 and the Constitution V mutation proof
are measured against. Nothing is edited in this phase.

- [X] T001 Record the baseline: run `bin/run-tests --filter 'fb2'` and save the passing suite list plus the `body-prefix.fb2` golden hashes from test/fb2_section_cache/Fb2SectionGoldenTest.cpp (`0x261012990916872b`, `0xb1edbf448284d2a1`) to the session scratchpad, so a later hash change is detected rather than rationalised.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: state the amended rule in the test oracle and add the missing fixture, before
any production code asserts it.

**⚠️ CRITICAL**: T002 deliberately turns the existing partition assertions RED
(test/fb2_book/Fb2BookTest.cpp:312, :425, :502 and test/fb2_metadata_parser/Fb2MetadataParserTest.cpp:213).
That red is the mutation proof for contract rule C7′ — do not "fix" it by reverting the
oracle; it goes green in Phase 3 when the parser is amended.

- [X] T002 Amend the `topLevelSectionBytes` oracle in test/fb2_common/Fb2TestSupport.h:156-177 so each reading body is counted from its `<body>` start tag: track the body tag's offset, and when the first top-level `<section>` of that body opens, start the span there instead of at the section tag. Keep it an independent re-derivation from the source text — do not call the parser.
- [X] T003 [P] Create fixture test/fb2/multi-reading-body.fb2: two `<body>` elements, neither carrying a `name` attribute, each with its own `<title>` and `<epigraph>` ahead of its first `<section>`, and two sections per body. Distinct marker words per body (FR-006 needs per-body attribution to be observable).
- [X] T004 [P] Register test/fb2/multi-reading-body.fb2 in the fixture lists that enumerate `test/fb2/` — test/fb2_section_parser/Fb2SectionParserTest.cpp:559 and test/fb2_section_cache/Fb2SectionGoldenTest.cpp:37 — so the new fixture is covered by the existing parity and golden sweeps.

**Checkpoint**: oracle states the intended rule; fixture exists; partition assertions are
red for the right reason.

---

## Phase 3: User Story 1 - The percentage moves while reading front matter (Priority: P1) 🎯 MVP

**Goal**: a reading body's bytes ahead of its first `<section>` count toward that body's
first chapter's progress weight (FR-001, FR-002).

**Independent Test**: on test/fb2/body-prefix.fb2 (body tag at 188, first `<section>` at
314), chapter 0's `length` is 236 B instead of 110 B and `bookSize` is 349 B instead of
223 B, so chapter 0's share of the book moves from 49.3% to 67.6%.

### Tests for User Story 1 (REQUIRED per Constitution Principle V) ⚠️

> Write these FIRST and confirm they FAIL before T008–T009.

- [X] T005 [P] [US1] Add a weight pin in test/fb2_metadata_parser/Fb2MetadataParserTest.cpp over test/fb2/body-prefix.fb2: chapter 0 `length == 236`, chapter 1 `length == 113`, chapter 1 `cumulativeLength == 349`, and chapter 0's `fileOffset == 314` (the `<section>` tag, NOT the body tag — the offset must not move).
- [X] T006 [P] [US1] Add a progress pin in test/fb2_book/Fb2BookTest.cpp over test/fb2/body-prefix.fb2: `calculateProgress(chapter0, 1.0f)` equals 236.0/349.0, `calculateProgress(chapter0, 0.0f)` is 0.0f, and `calculateProgress(chapter1, 1.0f)` is 1.0f (FR-004).
- [X] T007 [US1] Run `bin/run-tests --filter 'fb2_book|fb2_metadata_parser'` against the unamended parser and record that T005 and T006 fail with the old values (110 / 223), satisfying Constitution V's "demonstrate the mutation it catches".

### Implementation for User Story 1

- [X] T008 [US1] Add to lib/Fb2/Fb2/Fb2MetadataParser.h a `size_t bodyPrefixStart` member with a `NO_BODY_PREFIX` sentinel (per [data-model.md](data-model.md)), documented as "byte offset of the reading body's `<body>` tag, pending attachment to that body's first chapter".
- [X] T009 [US1] In lib/Fb2/Fb2/Fb2MetadataParser.cpp: set `bodyPrefixStart` to `XML_GetCurrentByteIndex()` where a `<body>` becomes a reading body (the `inBody = true` branch, ~:117-129, replacing the `ponytail:` comment that documented the ceiling); in the `<section>` branch (~:139-141) set `open.startOffset = (bodyPrefixStart < startOffset) ? bodyPrefixStart : startOffset` and reset the sentinel, leaving `open.info.fileOffset = startOffset` untouched; reset the sentinel again at `</body>` (~:296-307) so the next reading body gets its own prefix (FR-006, FR-008).

**Checkpoint**: T005–T006 green; the Phase 2 partition assertions green again; US1 is
independently demonstrable via `bin/run-tests --filter 'fb2_book|fb2_metadata_parser'`.

---

## Phase 4: User Story 2 - No book loses or gains text, and none re-reads wrong (Priority: P1)

**Goal**: prove the widened span leaked into nothing — not rendering, not chapter
boundaries, not auxiliary bodies, not the fallback, not malformed input (FR-003, FR-005,
FR-006, FR-007, FR-008).

**Independent Test**: every golden page hash in test/fb2_section_cache is byte-identical to
the T001 baseline, and the weight of every fixture without body front matter is unchanged.

### Tests for User Story 2 (REQUIRED per Constitution Principle V) ⚠️

- [X] T010 [US2] Restate `Fb2MetadataParser.SectionOffsetsPointAtTheSectionTags` **and `NestedChapterLengthsExcludeChildSpans`** (the latter was not anticipated here; it derives the parent's span from its own `<section>` tag and so encodes the old rule too — found by running T009) in test/fb2_metadata_parser/Fb2MetadataParserTest.cpp:36-58: keep `fileOffset == <section> tag` for every chapter, and assert the span end as `fileOffset + length == closeTagOffset + 10`, where `closeTagOffset` is the byte offset of that chapter's `</section>` and 10 is its length, for chapters that are not their body's first, while chapter 0's span is asserted from the `<body>` tag. Do not delete the assertion — it is the rule's statement.
- [X] T011 [P] [US2] Add an auxiliary-body pin in test/fb2_metadata_parser/Fb2MetadataParserTest.cpp over test/fb2/notes-body.fb2: the named body's front matter adds no weight, and total weight equals the reading body's alone (FR-005).
- [X] T012 [P] [US2] Add a per-body attribution pin over test/fb2/multi-reading-body.fb2: each body's front-matter bytes land in that body's own first chapter, and no chapter's weight includes another body's prefix (FR-006).
- [X] T013 [P] [US2] Add a fallback pin over test/fb2/no-sections.fb2 and test/fb2/wrapper-only.fb2: a reading body with no `<section>` keeps exactly its current whole-file weight, with no prefix added on top (FR-007).
- [X] T014 [P] [US2] Add a clamp pin over test/fb2/malformed-truncated.fb2 and test/fb2/wrong-root.fb2: every reported `length` and `cumulativeLength` stays below the file size and no value wraps, and the suite is clean under `bin/run-tests --asan` (FR-008, Constitution VI).
- [X] T015 [US2] Run `bin/run-tests --filter fb2_section_cache` and diff the golden hashes against the T001 baseline: unchanged is the pass condition for FR-003. A changed hash means the span widening reached the renderer — stop and fix rather than re-recording the hash.

**Checkpoint**: US1 and US2 both green; rendering provably untouched.

---

## Phase 5: User Story 3 - Books cached by the old firmware recover silently (Priority: P2)

**Goal**: a v5 cache is rejected and rebuilt with no user-visible error and no needless
relayout; the saved reading position survives (FR-009, FR-010).

**Independent Test**: a hand-built v5 `book.bin` is rejected, `book.bin` is rebuilt at v6,
`sections/` and `progress.bin` are still on disk, and a second open reuses the cache.

### Tests for User Story 3 (REQUIRED per Constitution Principle V) ⚠️

- [X] T016 [US3] Update the hand-built cache helper in test/fb2_common/Fb2TestSupport.h:183-220 to v6 (`V5Record`/`V5BookBin` → `V6Record`/`V6BookBin`, default `version = 6`) and update its users in test/fb2_book/Fb2BookTest.cpp, so "a valid cache" in the tests means a v6 cache.
- [X] T017 [P] [US3] Add a rejection pin in test/fb2_book/Fb2BookTest.cpp: a `book.bin` whose version byte is 5 is rejected and rebuilt, and the rebuilt chapter weights match a from-scratch parse (FR-009).
- [X] T018 [P] [US3] Add a pin that a v5 → v6 rejection does NOT remove `sections/` for a book with more than 256 chapters (research R3), so the bump costs one metadata parse and not a full relayout. In the same test body, write a `progress.bin` alongside the v5 cache and assert it is still present and byte-identical after the rejection (FR-010): `lib/Fb2` never opens that file — `src/activities/reader/ProgressFile.h` owns it — so the requirement holds precisely because the drop clause at lib/Fb2/Fb2.cpp:384 touches only `sections/`, and that is what this asserts.

### Implementation for User Story 3

- [X] T019 [US3] In lib/Fb2/Fb2.cpp add `constexpr uint8_t FB2_CAP_FREE_CACHE_VERSION = 5;` next to `FB2_OLD_CHAPTER_CAP` (~:25-31) and change the `sections/`-drop condition at :384 from `rejectedVersion < FB2_CACHE_VERSION` to `rejectedVersion < FB2_CAP_FREE_CACHE_VERSION`, with a comment that the clause targets caches built under the old 256-chapter cap, not every older version.
- [X] T020 [US3] In lib/Fb2/Fb2.cpp bump `FB2_CACHE_VERSION` from 5 to 6 (:25) and add the version-history line above it: "v6: a body's own content ahead of its first `<section>` is counted in that body's first chapter's length; record layout is unchanged, only the values."

**Checkpoint**: all three stories green; cache migration proven on host.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T021 [P] Check that T009's replacement of the `ponytail:` ceiling comment at lib/Fb2/Fb2/Fb2MetadataParser.cpp:127-131 reads as merged state — what the offset is for, not what it used to fail to do. T009 owns the edit; this is the review pass.
- [X] T022 [P] Close the loop on the contract: confirm [contracts/chapter-model.md](contracts/chapter-model.md) C7′/C10′ matches what shipped, and that specs/003's C7/C10 are reachable from it (the amendment header already points back).
- [X] T023 Run the full gates: `./bin/clang-format-fix -g`, `bin/run-tests`, `bin/run-tests --asan`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. On macOS also run `./bin/run-tests-linux`, since gates 2/3/5 here are weaker than CI's.
- [X] T024 Walk [quickstart.md](quickstart.md) §1–§5 end to end and record the observed numbers against the expected table.
- [X] T025 Device check on a real X4 (2026-09-24, firmware 1.6.0-bb.4-dev-feature/fb2-body-prefix-progress-b4601264). Run with a throwaway boot harness, since buttons cannot drive a device check and the C3 has no USB MSC to stage a chosen book. Over 601 FB2s on the card the largest body front matter is 1,167 B: chapter 0 measures 50,590 B (it would be 49,423 B unfixed), and the percentage across that front matter moves 0.00% -> 0.10% instead of holding. Idle free heap 138,264 B of 268,416 B, identical to the pre-change baseline. **Still outstanding for a human**: the visible page-by-page climb on a book whose front matter is a large share of the text — no such book is on this card, and staging one needs the SD reader.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: no dependencies; must precede T015 (it is the baseline T015 diffs against).
- **Foundational (T002–T004)**: blocks every story. T002 red-lights the partition assertions until T009.
- **US1 (T005–T009)**: after Foundational. Delivers the fix.
- **US2 (T010–T015)**: after US1 — its assertions describe post-fix behaviour.
- **US3 (T016–T020)**: after US1 (weights must be correct before a cache version claims them). Independent of US2.
- **Polish (T021–T025)**: after all stories.

### Within Each Story

- Tests before implementation, and shown to fail first (T007 is that step, explicitly).
- T008 (header) before T009 (source).
- T016 (helper rename) before T017/T018, which build caches with it.
- T019 before T020: pin the cap clause *before* bumping the version, so the bump never
  transiently drops `sections/` for long books.

### Parallel Opportunities

- T003 and T004 (fixture and its registrations) — different files.
- T005 and T006 — different suites.
- T011, T012, T013, T014 — independent pins, though T011–T014 all land in
  test/fb2_metadata_parser/Fb2MetadataParserTest.cpp, so serialise the final edit or split
  by clearly separated test bodies.
- T017 and T018 — different test bodies, same file; same caveat.
- T021 and T022 — different files.

### Parallel Example: User Story 1

```bash
# The two pins are independent suites and can be written together:
Task: "Weight pin on body-prefix.fb2 in test/fb2_metadata_parser/Fb2MetadataParserTest.cpp"
Task: "Progress pin on body-prefix.fb2 in test/fb2_book/Fb2BookTest.cpp"
```

---

## Implementation Strategy

### MVP (User Story 1 only)

T001 → T002–T004 → T005–T009. At that point the defect is fixed and provable:
`bin/run-tests --filter 'fb2_book|fb2_metadata_parser'` green, chapter 0 of
`body-prefix.fb2` weighing 236 B of a 349 B book. **Do not ship the MVP alone** — T015
(golden hashes) is what proves the fix did not disturb rendering, and T019–T020 are what
stop a device reading stale weights from a v5 cache.

### Incremental Delivery

1. Foundational + US1 → the percentage is correct on a fresh cache.
2. + US2 → proven to have changed nothing else.
3. + US3 → correct on devices that already have caches.
4. + Polish → gates, quickstart, device check.

### Commit Granularity (Constitution VII)

One logical change per commit, cherry-pickable onto a branch cut from `origin/develop`:

- `test:` the oracle, the fixture and the pins (T002–T007, T010–T014).
- `fix:` the parser change (T008–T009) — this is the upstream-bound commit.
- `fix:` the cache version and the cap-clause pin (T016–T020).
- `docs:`/`style:` polish (T021–T022).

Fork-only test suites stay out of any upstream PR branch.

---

## Notes

- 25 tasks: 1 setup, 3 foundational, 5 US1, 6 US2, 5 US3, 5 polish.
- The production diff is ~10 lines across 3 files. Most of the work is proving it changed
  nothing else — which, for a byte-arithmetic change under a persisted cache, is where the
  risk actually lives.
- Verify each pin fails before implementing it; T007 makes that a task rather than a habit.
- `bin/run-tests --filter <regex>` matches **suite directory names** (`fb2_book`), not
  gtest test names.

---

## Phase 7: Convergence

**Purpose**: remaining work found by assessing the code against spec.md, plan.md and the
constitution on 2026-09-24, after T001–T025 were implemented. Every functional requirement
is met and pinned; what is missing is evidence for the two corpus-scale success criteria,
which the 568-byte fixture and the device card's 1,167-byte maximum cannot supply.

- [X] T026 Verify SC-001 on the corpus worst case, which no target has yet opened: copy `~/Calibre Library/Po/Moie tielo - Bosfor (36919)/Moie tielo - Bosfor - Po.fb2` (256,971 B of body front matter against 403,143 B of sections — 38.9% of the book) into `fs_branches/_books/`, run `bin/run-simulator`, open it, and page forward through the front matter recording the displayed percentage at the first page, at the last page before the first `<section>`, and one page after it. The percentage must climb across the front matter and cross into the section without a jump. The simulator runs the same reader core, so this closes the criterion the device could not: T025 found the card's largest front matter is 1,167 B (0.10%), which proves the mechanism but not this criterion per SC-001 (partial) **Done (2026-09-24, simulator, the branch build at ad9329ea)**: body tag at 540, first `<section>` at 257,511, chapter 0 spans 540->258,371, so front matter is 99.7% of chapter 0 and ends at page 317 of 318. Displayed percentage: page 1/318 = 0%, page 41/318 = 5%, page 317/318 (last front-matter page) = 39%, page 318/318 = 39%, first page of chapter 1 = 1/1 39%. It climbs across the front matter and crosses into the section with no jump, matching SC-001's 38.9% figure. **Confirmed on real hardware (2026-09-25, X4, firmware 1.6.0-bb.5)**: the same book was uploaded to the device's SD card over the WiFi file-transfer page (660,675 B verified on the card) and read on the device. Chapter 0 is 236 pages there (a smaller viewport than the simulator's 318) and the status bar reads 0% at page 1, 1% at page 8, 5% at page 30 and 39% at page 236 — the last page of the front matter. Before this change all 236 pages read 0%. This closes the gap T025 left open for a human.
- [X] T027 Spot-check SC-002, which no target has exercised: pick one corpus book whose front matter is worth at least half a displayed percentage point (the 2026-09-22 scan found 165 such books, 5.7%), open it in the simulator as in T026, and record that the percentage shown at the end of its front matter differs from the percentage at its start. Alternatively re-run the corpus scan against the amended parser and confirm the 165-book population still holds post-change — the figure currently rests on the pre-change scan alone per SC-002 (partial) **Done (2026-09-24, simulator)**: `Giermanskii prikhvostien', ili Moskovskii - Vol'fghangh Akunov.fb2` (2.11% share; prefix 3,226 B of a 3,334 B chapter 0, i.e. 96.8% front matter). Chapter 0 ("Unnamed") is 5 pages and reads 0% -> 1% -> 1% -> 2% -> 2%; the next page enters chapter 1 at 1/10 3%. The percentage at the end of the front matter differs from the percentage at its start, which is what SC-002 asks for.
- [X] T028 Reconcile the spec's KOReader assumption (spec.md:155) with what research R4 established: the assumption states that progress synchronisation "consumes the same percentage and therefore follows this change automatically", but `lib/KOReaderSync/` carries no FB2 path at all — `ProgressMapper` is EPUB-only (`ProgressMapper.cpp:837`) — so the conclusion (no mapping work) is right while the stated mechanism is not. Correct the assumption to say there is nothing to follow per research R4 (contradicts) **Done**: spec.md's assumption now reads that `lib/KOReaderSync/` carries no FB2 path at all, so there is nothing to follow this change rather than something that follows it automatically.
