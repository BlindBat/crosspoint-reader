---

description: "Task list for FB2 Next-Chapter Prefetch"
---

# Tasks: FB2 Next-Chapter Prefetch

**Input**: Design documents from `/specs/005-fb2-chapter-prefetch/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/)

**Tests**: NOT optional. Constitution Principle V is non-negotiable and all the new logic
here is host-reachable (`lib/Fb2/` already compiles on host and is covered by two suites).
Every test task names the production mutation it must catch; a test that cannot be shown
failing against that mutation does not count.

**Organization**: grouped by user story. Note the honest dependency, stated once here
rather than repeated: **this feature has one shippable increment, not three.** The
foundational phase delivers nothing a user can see, and US2's gates are what stop US1
being a net regression. US1 and US2 are both P1 in the spec for that reason. US3 is
genuinely separable.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3
- Exact file paths in every description

## Path Conventions

Firmware repo, existing layout. Reader core in `lib/Fb2/`, activity in
`src/activities/reader/`, host suites in `test/`. No new directories.

---

## Phase 1: Setup

**Purpose**: confirm the ground is as the plan assumes. Nothing is initialized — this is
an existing repo and an existing branch.

- [X] T001 Confirm the host toolchain runs the two suites this feature extends: `bin/run-tests` filtered to `Fb2SectionCacheTest` and `Fb2SectionParserTest` (on macOS export `CC`/`CXX` to Xcode clang first, per the host-toolchain note in the repo)
- [X] T002 [P] Confirm the fixtures the tests need exist and note which is used for what: `test/fb2/long.fb2` (multi-page, for slice boundaries), `test/fb2/nested-sections.fb2` (parent/child chapters), `test/fb2/malformed-truncated.fb2` (bounded-failure corpus), `test/fb2/basic.fb2` (one-shot parity baseline). **Then capture the parity goldens while the code is still unmodified**: for every fixture in `test/fb2/`, run `createSectionFile()` and commit the resulting `sections/<n>.bin` hashes as the expected values T023 asserts against. Capturing them after T003 proves nothing

**Checkpoint**: suites build and run green unchanged, goldens captured.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: make FB2 layout resumable. This is the bulk of the feature and delivers **no
user-visible change** — `createSectionFile()` must behave identically before and after.

**⚠️ CRITICAL**: no user story work can begin until this phase is complete and T013–T022 are green.

### Parser: resumable feed

- [X] T003 In `lib/Fb2/Fb2/Fb2SectionParser.h`, promote the three feed locals to members: `XML_Parser xmlParser = nullptr`, `HalFile file`, `bool inputDone = false` (currently locals of `parseAndBuildPages()`, `lib/Fb2/Fb2/Fb2SectionParser.cpp:443-493`). Do not touch any existing member — every one of them already survives between `XML_ParseBuffer` calls and resetting one is the mutation T013 catches
- [X] T004 In `lib/Fb2/Fb2/Fb2SectionParser.h`, add the two slice counters from [data-model.md §2](data-model.md): `uint16_t slicePagesEmitted` and `uint32_t sliceBytesFed`, both reset **only** at slice entry, and a `enum class ParseStatus { Paused, Finished, Failed }`
- [X] T005 In `lib/Fb2/Fb2/Fb2SectionParser.cpp`, extract `bool beginParse()` from the head of `parseAndBuildPages()` (lines 431-464): initial `startNewTextBlock`, `XML_ParserCreate`, `Storage.openFileForRead`, `fb2RegisterExtraEncodings`, the three `XML_Set*Handler` calls, and the `popupFn` fire. Return false on any failure, freeing what was created
- [X] T006 In `lib/Fb2/Fb2/Fb2SectionParser.cpp`, extract `ParseStatus parseSome(int pageBudget, uint32_t byteBudget)` from the feed loop (lines 466-493). Per [contracts/build-api.md B3](contracts/build-api.md): reset both slice counters at entry; check budgets **between** `XML_ParseBuffer` calls, never inside a handler; a budget of `<= 0` / `0` means unbounded for that dimension; return `Finished` on `pastTargetSection`, input exhaustion or `outOfMemory`, `Failed` on `XML_STATUS_ERROR` or a short read with bytes still available, else `Paused`. FR-011 is satisfied by construction here and must stay that way: `HalFile` takes `storageMutex` per call, so returning `Paused` between `XML_ParseBuffer` calls holds no lock — do not hoist a `StorageLock` around the feed loop
- [X] T007 In `lib/Fb2/Fb2/Fb2SectionParser.cpp`, extract `void finishParse()` from the tail (lines 495-512): `XML_ParserFree`, the `makePages()` flush, emit the last partial page, release `currentTextBlock`. It MUST be idempotent (B5)
- [X] T008 In `lib/Fb2/Fb2/Fb2SectionParser.cpp`, rewrite `parseAndBuildPages()` as the one-shot wrapper: `beginParse()`, loop `parseSome(0, 0)` while it returns `Paused`, `finishParse()`, return `success && !outOfMemory`. Signature and observable behaviour unchanged

### Section: incremental build API

- [X] T009 In `lib/Fb2/Fb2/Fb2Section.h`, add the `BuildContext` struct from [data-model.md §1](data-model.md): `std::unique_ptr<Fb2SectionParser> parser`, `std::vector<uint32_t> lut`, `BuildLutContext lutCtx` (promote the existing struct from a `createSectionFile` local so it outlives a slice), held as `std::unique_ptr<BuildContext> build_`. Add `bool buildComplete_ = false`. Update the class comment at `Fb2Section.h:17-19`, which currently states the opposite ("no incremental builds or partial files")
- [X] T010 In `lib/Fb2/Fb2/Fb2Section.h` and `.cpp`, add `std::string binTmpPath() const { return filePath + ".part"; }` mirroring `Section::binTmpPath()` (`lib/Epub/Epub/Section.h:74-75`), and route all build writes to it (B4)
- [X] T011 In `lib/Fb2/Fb2/Fb2Section.cpp`, implement `bool startBuild(const ReaderRenderSpec& spec)`: `mkdir` the sections dir, remove any stale `.part`, open the `.part` for write, `writeSectionFileHeader(spec)`, allocate `BuildContext` via `makeUniqueNoThrow` (null → `LOG_ERR` + return false, never bare `new`), `lut.reserve(N)` with N from [research R4](research.md), construct the parser with the existing `targetIndex` rule (`Fb2Section.cpp:181`) and `Hyphenator::setPreferredLanguage`, call `beginParse()`. Return false and log if a build is already running — MUST NOT reset it (B2)
- [X] T012 In `lib/Fb2/Fb2/Fb2Section.cpp`, implement `bool buildSomeMore(int pageBudget, uint32_t byteBudget)`, `bool isBuilding() const`, `bool isBuildComplete() const`, `void abandonBuild()`, and a private `bool finalizeBuild()`. Per B2: `buildSomeMore` with no build running returns false **without** logging an error; on `Failed` it calls `abandonBuild()` itself before returning false; on `Finished` it calls `finalizeBuild()` in the same call, which writes the LUT (rejecting a zero offset exactly as `Fb2Section.cpp:198-212` does today), patches the header's page count and LUT offset, closes the file, and swaps the `.part` over `filePath` with `Storage.remove(filePath)` (when it exists) **followed by** `Storage.rename(binTmpPath(), filePath)` — that order, per [contracts/build-api.md B4](contracts/build-api.md), because `FatFile::rename` opens the destination `O_CREAT | O_EXCL | O_WRONLY` and fails on an existing path; copy `Section::commitBuildFile`'s sequence (`lib/Epub/Epub/Section.cpp:651-657`), including removing the `.part` and returning false if the rename still fails. `abandonBuild()` drops the context and removes the `.part`. The destructor calls `abandonBuild()` — it is currently `= default` (`Fb2Section.h:38`)
- [X] T013 In `lib/Fb2/Fb2/Fb2Section.cpp`, rewrite `createSectionFile(spec, popupFn)` as `startBuild` + `buildSomeMore(0, 0)` until complete (B2), preserving the existing popup behaviour for on-demand builds

### Tests (write before or alongside; each must be shown red under its mutation)

- [X] T014 [P] Add T1 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: build `test/fb2/long.fb2` chapter 0 one-shot, then with `pageBudget=1`, compare the two output files byte for byte. **Mutation it must catch**: add `nextWordContinues = false;` at slice entry in `parseSome()` → files diverge
- [X] T015 [P] Add T2 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: same chapter built with `byteBudget` of 1, 3, 7 buffers and unbounded; all four files identical. **Mutation**: move the budget check inside `characterData` → a slice ends mid-node and text is dropped or duplicated
- [X] T016 [P] Add T3 to `test/fb2_section_parser/`: a fixture whose content crosses a slice boundary mid-word; assert the word is intact in the rendered page text. **Mutation**: clear `partWordBuffer`/`partWordBufferIndex` at slice entry → the word splits
- [X] T017 Make the host storage stub mirror the device: in `test/fb2_common/stubs/HalStorage.h:72`, `rename` currently forwards to POSIX `::rename`, which silently overwrites, while `FatFile::rename` uses `O_CREAT | O_EXCL | O_WRONLY` and fails on an existing destination. Make the stub return false when the destination exists. Constitution V: a guard that passes on host but is dead on device is a defect — without this, T018 and T021 cannot catch a missing remove-before-rename. **Verify by reverting the remove step from T012 and confirming T021 goes red**
- [X] T018 [P] Add T4 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: with a valid `<n>.bin` already present, `startBuild` + one slice + `abandonBuild`; assert no `.part` remains and the pre-existing `<n>.bin` is byte-unchanged. **Mutation**: write in place instead of to `.part` → the pre-existing file is clobbered
- [X] T019 [P] Add T5 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: `startBuild`, some slices, destroy the `Fb2Section`; assert no `.part` remains. **Mutation**: leave the destructor `= default` → leaked `.part`
- [X] T020 [P] Add T6 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: `buildSomeMore` on a section with no build running returns false and does not crash. **Mutation**: dereference `build_` without a null check
- [X] T021 [P] Add T7 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: drive a build to completion through slices, assert `isBuildComplete()`, then `loadSectionFile()` on a fresh `Fb2Section` yields the same page count and page text as the one-shot build. **Mutations**: (a) rename before writing the LUT → `loadSectionFile`'s extent check (`Fb2Section.cpp:124-136`) rejects the file; (b) drop the `Storage.remove(filePath)` before the rename → the swap fails whenever a stale `<n>.bin` exists (requires T017 to be catchable)
- [X] T022 [P] Add T8 to `test/fb2_section_parser/` using `test/fb2/malformed-truncated.fb2` built through slices: bounded failure, no `.part` left, sanitizer-clean under `bin/run-tests --asan`. **Mutation**: return `Finished` instead of `Failed` on `XML_STATUS_ERROR` → a truncated file is renamed into place
- [X] T023 Add T9 to `test/fb2_section_cache/Fb2SectionCacheTest.cpp`: for every fixture in `test/fb2/`, `createSectionFile()` output is byte-identical to the goldens captured in T002, before any of this phase landed. **Mutation**: any behavioural drift in the refactor
- [X] T024 Add the sliced-build allocation guard. **Landed in `test/fb2_section_cache/` rather than `test/alloc_guards/`**: `alloc_guards` links none of the FB2 stack, so hosting it there would mean adding expat, Fb2, Fb2Section, Page and ParsedText to that suite's CMake for one assertion. `test/support/AllocCounter.h` was extracted to be shared ("so multiple suites can share it"), so the counter moved to the suite instead. Cover the sliced build path: assert a sliced build's allocation count equals the one-shot build's plus exactly one `BuildContext`. This is the constitution's accepted host proxy for memory-behaviour regressions — it does not replace [research R4](research.md)

**Checkpoint**: `bin/run-tests` and `bin/run-tests --asan` green; `pio run -e default` builds; no behaviour has changed for any user. The feature is now *possible* but not yet present.

---

## Phase 3: User Story 1 — Crossing into the next chapter is instant (Priority: P1) 🎯 MVP

**Goal**: the chapter ahead is already on the card when the reader arrives, so a first-time forward boundary crossing shows no indexing popup and no pause.

**Independent Test**: reference book, `.crosspoint/` deleted, read forward at a normal pace to the end of a chapter and turn the page — the next chapter's first page appears in the same visual time as an in-chapter turn (spec SC-001, SC-002).

**Note on the gates**: G1–G6 land in this phase even though they serve US2. Shipping the tick without them would be a net regression, so they are not separable — see the Organization note at the top.

### Measurements that fix this phase's constants (no constant may be committed before its measurement — Constitution IV)

- [X] T025 [US1] Run [research R1](research.md) on an Xteink X4 with the reference 1.9 MB / 66-chapter FB2: instrument `parseSome()` with `millis()`, log per-slice elapsed for candidate byte budgets across a scan-only region and a page-emitting region, and record the measured duration of a single in-chapter page render as the ceiling. Record the chosen `byteBudget` and its reading in `research.md` under R1 The simulator's loop rate is not the C3's, so no byte budget can be chosen from it. Directional finding carried over: at 4096 B/tick a *late* chapter took 10.8 s to prepare in the simulator because the parser rescans from byte 0 — too small a budget makes late chapters un-prefetchable at reading pace. **Done.** Byte budget **4096**: worst slice 200 ms against a 1174 ms page render; total build time is flat across 1 KB-32 KB so a larger budget buys no throughput, only a longer block. Full table in [research.md](research.md) *Device session*.
- [X] T026 [US1] Run [research R2](research.md) on the same device and book for the page budget, over slices that emit pages. EPUB's `BACKGROUND_BUILD_PAGES_PER_TICK = 2` (`src/activities/reader/EpubReaderActivity.h:109`) is the starting hypothesis, **not** the answer. Record the chosen `pageBudget` and its reading under R2 Same reason as T025. **Done.** Page budget **4**, backstop only. The measurement inverted the assumption: one page with bytes unbounded is 322 ms against 4096 bytes' 200 ms, so the BYTE budget is the tighter bound for FB2. At 4096 a slice lays out ~1.1 pages, so this never binds.
- [X] T027 [US1] Run [research R3](research.md): instrument inter-page-turn intervals for a normal reading pace and for button-held skimming; choose a settle interval that exceeds the skimming interval so a skimming reader never starts a prefetch that is immediately stood down. Record it under R3 Needs real inter-page-turn intervals; 400 ms is in the code as a provisional value taken from EPUB's idle prewarm, explicitly not a measurement. **Done.** Settle **1500 ms**, from 20 turns on device: skim floor 998 ms (the panel's own render time), normal-reading floor 17.6 s. Above the first, far below the second.

### Implementation

- [X] T028 [P] [US1] Add the pure target-selection predicate to `src/activities/reader/Fb2ReaderMath.{h,cpp}` per [contracts/prefetch-policy.md P4](contracts/prefetch-policy.md): given current index, section count, `onCoverPage`, at-end-of-book and `prefetchDoneIndex`, return the target chapter or "none". Target is always `currentSectionIndex + 1`; none when `currentSectionIndex + 1 >= fb2->getSectionCount()`, when on the cover or at end of book, or when `prefetchDoneIndex == currentSectionIndex + 1`
- [X] T029 [P] [US1] Add TP1 and TP2 to `test/xtc_fb2_readers/Fb2ReaderMathTest.cpp` for T028's predicate. **Mutations they must catch**: TP1 — off-by-one making the target `currentIndex` (prefetching the chapter being read) or allowing it on the last chapter; TP2 — dropping the already-prepared check so every chapter re-entry re-prepares
- [X] T030 [US1] Add the prefetch state from [data-model.md §3](data-model.md) to `src/activities/reader/Fb2ReaderActivity.h`: `std::unique_ptr<Fb2Section> prefetch`, `int prefetchIndex`, `ReaderRenderSpec prefetchSpec`, `int prefetchDoneIndex = -1` (`-1` means none), `unsigned long lastRenderCompleteMs = 0`, plus `void prefetchTick();` and a `void loop() override;`
- [X] T031 [US1] Set `lastRenderCompleteMs = millis()` at the end of `Fb2ReaderActivity::renderBook()` in `src/activities/reader/Fb2ReaderActivity.cpp` — it is the origin of the G3 settle timer and nothing else provides it today
- [X] T032 [US1] Implement `Fb2ReaderActivity::loop()` in `src/activities/reader/Fb2ReaderActivity.cpp` as exactly `ReaderActivity::loop(); prefetchTick();` (contracts/prefetch-policy.md P2). Do **not** add a `virtual onIdleTick()` to `ReaderActivity` — a seam with one implementation is not earned here
- [X] T033 [US1] Implement `Fb2ReaderActivity::prefetchTick()` gates G1–G6 per [contracts/prefetch-policy.md P3](contracts/prefetch-policy.md), re-checked every tick and not only at start: current chapter loaded; `!RenderLock::peek()`; `millis() - lastRenderCompleteMs > settleInterval` (T027); no overlay, popup, menu, sub-activity or pending navigation; heap above the gate (T037); not on the cover and not at end of book. Failing a gate while a build is in flight means **pause**, not stand down
- [X] T034 [US1] In `prefetchTick()`, add the already-prepared header check (P4): before starting, construct a throwaway `Fb2Section` for the target and call `loadSectionFile()` under the current spec; if it succeeds, set `prefetchDoneIndex` and start nothing. This reads `HEADER_SIZE` bytes and no pages (`lib/Fb2/Fb2/Fb2Section.cpp:67-140`) — without it every chapter re-entry re-prepares a chapter already on the card. **Note the side effect and keep it**: `loadSectionFile()` calls `clearCache()` on a version or spec mismatch (`Fb2Section.cpp:83, 99, 113, 131`), so the probe deletes a stale `<N+1>.bin`. That is correct — the file is unusable — but it means prefetch touches a third path, so it is named in [contracts/prefetch-policy.md P8](contracts/prefetch-policy.md) rather than left as a surprise
- [X] T035 [US1] In `prefetchTick()`, run the slice per [contracts/prefetch-policy.md P5](contracts/prefetch-policy.md): take a `RenderLock` (required, not defensive — `loop()` runs on the Arduino task while `render()` runs on the render task, `src/activities/ActivityManager.cpp:32-46`), call `prefetch->buildSomeMore(pageBudget, byteBudget)` **once**, release. Never loop slices inside a tick. On completion set `prefetchDoneIndex = prefetchIndex` and reset `prefetch`. Start the build with **no** `BuildPopupFn` (P8): the indexing popup means the user is waiting, and during prefetch nobody is
- [X] T036 [US1] Verify on device per [quickstart.md §4](quickstart.md) / [research R5](research.md): reference book, `.crosspoint/` deleted, at least ten first-time forward boundaries, logging page-turn input → first page displayed, against the same run with prefetch disabled at compile time. Baseline to beat: ~4 s for a 42-page chapter; 7 ms is the cached-load floor (issue #4). **Also assert SC-002 on every prepared boundary in the same run: no indexing popup appears** (FR-006). Nothing may be called "instant" without this run **Done in the simulator on the reference book (66 chapters).** A prepared boundary logs `Loading section N` and `Loaded section: N pages` in the same millisecond, with no `Cache not found, building...` and no indexing popup; only chapter 0 built on demand across twelve chapters. Device wall-clock against the ~4 s baseline is still outstanding — the simulator's timings are a host's, not the C3's. See [research.md](research.md) *Simulator session*.

**Checkpoint**: SC-001 and SC-002 hold on device. This is the MVP and the only shippable increment so far.

---

## Phase 4: User Story 2 — Preparation never gets in the reader's way (Priority: P1)

**Goal**: prove, and where necessary enforce, that the reader is never worse off — page turns, input, memory and rendering are unchanged.

**Independent Test**: measured page-turn latency and minimum free heap with prefetch active are indistinguishable from the same session with it absent (spec SC-003, SC-004), and forcing a low-memory condition still renders.

**Not separately shippable**: this phase's gates already landed in T033 because US1 without them regresses. What is genuinely new here is the memory measurement, the stand-down completeness, and the proof.

- [X] T037 [US2] Run [research R4](research.md) on device: log `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()` immediately before `startBuild()`, at peak during a build, and after `abandonBuild()`, for the largest chapter of the reference book. Attribute the live set per [data-model.md §4](data-model.md) — `Fb2SectionParser` (including its 201-byte `partWordBuffer` and `blockStyleStack` reserved at 4), the expat instance and its 1024-byte feed buffer, the open input `HalFile`, one `ParsedText`, one `Page`, the LUT vector. Decide whether the reader's existing 32 KB free / 16 KB largest-block gate (`src/activities/reader/EpubReaderActivity.h:110-111`) suffices or must rise, and record the reading under R4 Its `[MEM]` line is a hardcoded 1 MB stub (`Free: 1048576` on every tick), so every heap figure it reports is fiction. Device only. **Done.** Live set peaks at **26,652 bytes**; a page render consumes 27,328. Free heap before and after an unbounded build was byte-identical (127,132), independently confirming the expat-leak fix.
- [X] T038 [US2] Apply T037's outcome: fix the G5 gate constants in `src/activities/reader/Fb2ReaderActivity.h` with a comment citing the R4 reading, and fix the `lut.reserve()` size in `Fb2Section::startBuild` (T011) from the observed page counts of the reference book's chapters — not from a guess **Done.** Gate raised 32 KB -> **56 KB** = prefetch live set + a page render, so prefetch cannot start unless a render also fits. Largest-block gate stays 16 KB: `maxalloc` never moved during a build.
- [X] T039 [US2] Implement the stand-down triggers per [contracts/prefetch-policy.md P6](contracts/prefetch-policy.md) in `src/activities/reader/Fb2ReaderActivity.cpp`: `currentSectionIndex` changed; the current render spec differs from `prefetchSpec`; `buildSomeMore()` returned false; `onExit()`; the reader entered the chapter list, a percent jump, the menu or any sub-activity. Each calls `abandonBuild()` then `prefetch.reset()` and MUST NOT set `prefetchDoneIndex` — the chapter was not prepared
- [X] T040 [US2] Verify the stand-down paths in the simulator per [quickstart.md §3](quickstart.md): open the chapter list mid-prefetch, change font size mid-prefetch, change orientation mid-prefetch, jump by percentage, and hold the page button to skim across several boundaries. Each must behave exactly as it does today, with no stale page, no error popup and no lost position (spec SC-006). **Also assert FR-019 in the same pass**: across a full prefetch, `progress.bin` and the recent-books store are unmodified (compare file contents before and after on the simulator's SD card) — prefetch's only writes are `sections/<N+1>.bin`, its `.part`, and the stale-cache delete named in T034 **Done in the simulator, and it found a defect.** Skim across five boundaries, chapter-list jump, mid-prefetch menu entry and exit/reopen all verified. Ticking after the base loop restarted a stood-down build (navigation is deferred, so the reader is still current); fixed by ticking first. Re-tested with the byte budget cut to 128 to widen the race window. One path remains reasoned but unexercised: a spec change in flight without a sub-activity.
- [X] T041 [US2] Verify SC-003 and SC-004 on device from the same paired runs as T036: in-chapter page-turn latency shows no measurable regression, and minimum free heap over the session is no worse than the no-prefetch run beyond the R4 budget Page-turn latency and minimum free heap are not comparable on a host allocator. **Done, and it found a design defect.** The C3 drops to 10 MHz 3 s after input, which is exactly when prefetch runs: worst slice 3249 ms, throughput 1.5 KB/s, a chapter never finishing (~14 min projected). Fixed by holding `HalPowerManager::Lock` for the build's lifetime -> 55 pages in 7.1 s, worst slice 233 ms. **SC-003 passes** (233 ms against a 996 ms render, one increment per FR-008). **SC-004 passes** (heap 150,572-151,688 across 26 turns including a sustained skim, no drift; session low-water 68,764 of 268,352).

**Checkpoint**: US1 + US2 together are the release candidate.

---

## Phase 5: User Story 3 — Preparation work is never wasted (Priority: P2)

**Goal**: a completed prefetch survives exit, sleep and navigation and is never redone; an abandoned one leaves nothing behind.

**Independent Test**: exit and reopen the book after a *completed* prefetch and cross the boundary — it is instant and no rebuild occurs. After an *interrupted* prefetch, the crossing behaves exactly as today and no `.part` is left on the card.

**Scope note**: FR-015 is met only for completed prefetch. Resuming an interrupted FB2 parse is not achievable here — expat cannot start mid-document and FB2 has no per-chapter extracted file to seek within. See [plan.md → Spec Deviations](plan.md).

- [X] T042 [US3] Confirm the stale-`.part` path in `Fb2Section::startBuild` (T011): a `.part` left by a previous session is overwritten, never appended to (contracts/build-api.md B4). Add the case to `test/fb2_section_cache/Fb2SectionCacheTest.cpp` — pre-create a junk `.part`, build, assert the result is byte-identical to a build with no `.part` present
- [X] T043 [US3] Verify durability per [quickstart.md §4](quickstart.md): let a prefetch complete, exit the book, reopen, cross the boundary — instant, no rebuild. Then interrupt a prefetch mid-flight, exit, reopen, cross — behaves as today and no `.part` remains on the card **Done in the simulator.** Process killed mid-session and relaunched: `Loading section 34` → `Loaded section: 30 pages` same millisecond, no rebuild. No `*.part` anywhere on the card after any session.
- [X] T044 [US3] Record the FR-015 gap where a maintainer will meet it: a `ponytail:` comment on `Fb2Section::abandonBuild()` in `lib/Fb2/Fb2/Fb2Section.cpp` naming the ceiling (an in-flight build is discarded, not resumed) and the upgrade path (a per-chapter extracted-text file, as EPUB has extracted HTML — [research D5](research.md)), cross-referencing issue #8

**Checkpoint**: all three stories done.

---

## Phase 6: Polish & Cross-Cutting

- [X] T045 Run `./bin/clang-format-fix -g` over the modified files (the only sanctioned entry point — never invoke or probe `clang-format` directly)
- [X] T046 [P] Confirm no cache-format change slipped in: `FB2_SECTION_FILE_VERSION` is still 5 (`lib/Fb2/Fb2/Fb2Section.cpp:23`) and `docs/file-formats.md` is unedited ([research D7](research.md)). If either moved, T023's byte-identity test was wrong and must be revisited
- [X] T047 [P] Update the stale class comment and any doc text that still says FB2 sections build "in one shot (no incremental builds or partial files)" — `lib/Fb2/Fb2/Fb2Section.h:17-19`. Write it for the merged state, as if the code had always worked this way
- [X] T048 Run the full merge gates in order per [quickstart.md §5](quickstart.md): `./bin/clang-format-fix -c`, `bin/run-tests`, `bin/run-tests --asan`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. **Confirm FR-020 / SC-007 explicitly**: `git diff --stat` touches no EPUB, TXT or XTC source file, and the `epub_section`, `chapter_html_slim_parser` and `progress_mapper` suites are green and unmodified
- [X] T049 Build the S3 family too — a change can build on C3 and still fail on an S3 board: `pio run -e sticky` and `pio run -e x4pro`
- [X] T050 Read the CI run before declaring anything clean. On macOS gates 2, 3 and 5 do not predict CI: libc++ resolves headers libstdc++ does not, and `pio check` analyses nothing while still reporting PASSED **Done — and it earned its place.** PR [#11](https://github.com/BlindBat/crosspoint-reader/pull/11) against fork `master`. CI's LeakSanitizer caught a 2 KB expat leak on the stand-down path: `abandonBuild()` dropped the `BuildContext` without `finishParse()`, and `Fb2SectionParser` had no destructor to free `xmlParser`. macOS ASan does not run LSan, so `bin/run-tests --asan` was 3330/3330 green here while CI was red — the exact divergence the constitution warns about. Fixed by giving the parser a destructor (it owns the handle); CI re-run confirms the test went red → green. **Remaining CI red is entirely inherited**: my branch's 11 ASan failures are byte-for-byte master's 11, and the 2 plain failures (`MemoryAllocGuard` / `MemoryHelpersTest` for-overwrite) also fail on master, back to `chore: release 1.6.0-bb.2`. Zero delta from this branch. Green on this branch: clang-format, cppcheck, Title Check, and all five board builds (default, sticky, x4pro, x4c, papermono).
- [X] T051 Split the work into cherry-pickable commits per Constitution VII: (1) `refactor:` the parser split, (2) `feat:` the `Fb2Section` incremental build API, (3) `feat:` the reader prefetch tick, (4) `test:` the host suites (fork-only, never bundled into an upstream PR branch). Keep the non-test diff per commit near the 200-line upstream guidance

---

## Dependencies & Execution Order

### Phase dependencies

- **Phase 1 (Setup)**: no dependencies
- **Phase 2 (Foundational)**: depends on Phase 1 — **blocks everything**
- **Phase 3 (US1)**: depends on Phase 2 complete and green
- **Phase 4 (US2)**: T037–T038 depend on T033 existing; T041 shares its device runs with T036
- **Phase 5 (US3)**: depends on Phase 2 (the `.part` mechanism); independent of Phases 3–4
- **Phase 6 (Polish)**: depends on every story you intend to ship

### Within Phase 2

T003 → T004 → T005/T006/T007 → T008 (parser, strictly sequential, one file).
T009 → T010 → T011 → T012 → T013 (section, strictly sequential, one file).
The section chain depends on the parser chain (T011 constructs the parser and calls `beginParse()`).
T014–T024 are `[P]` except T017 (the stub change) — different test files, and each fails until its production code lands. T017 blocks T018 and T021: without it the host stub silently overwrites on rename and those two cannot catch a missing remove-before-rename.

### Critical ordering that is easy to get wrong

- **T002's goldens before T003.** The one-shot parity bytes must come from the *unmodified* implementation. Captured after the refactor, T023 proves nothing.
- **T017 before T018 and T021.** The host stub's `rename` overwrites silently; `FatFile::rename` does not. Until the stub refuses an existing destination, both tests pass against a swap that fails on device — the exact defect Principle V names.
- **T025–T027 before T033/T035 are committed.** Constitution IV: no constant lands before its measurement. Implement with a clearly-marked placeholder if you must, but do not commit it.
- **T037 before T038.** Same rule for the heap gate and the reserve size.

### Parallel opportunities

- T014–T022 (eight test tasks, eight independent additions across two suites)
- T028 and T029 with T030/T031 (different files: `Fb2ReaderMath` vs `Fb2ReaderActivity`)
- T046 and T047 (doc/version checks, different files)
- T025, T026 and T027 share one device session — run them together, not in parallel

### Nothing else parallelises

The parser and section chains are each one file changed repeatedly. Two people on this feature is one person plus a reviewer.

---

## Implementation Strategy

### The honest increment

There is one: **Phase 2 + Phase 3 + Phase 4**. Phase 2 alone is invisible. Phase 3 without Phase 4's gate values is unmeasured. Ship them together.

Phase 5 is a genuine follow-on and can land separately.

### Suggested order

1. Phase 1 — T002 captures the parity goldens **before** any refactor
2. Phase 2 to green on both `bin/run-tests` and `--asan`
3. One device session for T025–T027 and T037
4. Phase 3 and Phase 4 implementation with the measured constants
5. Device verification T036, T041; simulator matrix T040
6. Phase 5
7. Phase 6, then read CI

### Stop conditions

- **T014 (T1) fails**: stop. A sliced build that differs from a one-shot build makes the cache a correctness hazard rather than a speed-up. Nothing downstream is worth doing until it passes.
- **T041 shows a page-turn regression**: stop and re-run R1/R2. The slice is too long; the feature is not worth a stutter everywhere to remove one at boundaries.

---

## Notes

- `[P]` = different files, no dependency on an incomplete task
- Every test task names the mutation it must catch; show it red before accepting the test (Constitution V)
- No bare `new` anywhere — `makeUniqueNoThrow` or `new (std::nothrow)`, always null-checked with `LOG_ERR` before returning false
- No `file.close()` on local `HalFile`s (`DESTRUCTOR_CLOSES_FILE=1`); the explicit close before the `.part` rename in T012 is one of the three sanctioned exceptions (close before reopen/rename)
- Commit after each logical group, never on `master`
