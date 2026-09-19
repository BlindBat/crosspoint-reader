---

description: "Task list for 002-stub-cover-sleep-screen"
---

# Tasks: Stub Cover Sleep Screen

**Input**: Design documents from `/specs/002-stub-cover-sleep-screen/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [quickstart.md](quickstart.md)

**Tests**: The project override (Constitution Principle V) makes host tests mandatory for
host-reachable logic. This feature adds none — the wrapping engine it reuses is already covered by
`test/gfx_renderer/GfxRendererTest.cpp` and `GfxRendererAllocTest.cpp`, and what remains is render
calls inside an activity the host cannot compile. The rejection of an extraction-for-testability is
recorded in [research.md](research.md) §4 and in the plan's Complexity Tracking table, per the
override's "justify" clause. Verification is constitution gate 6 (simulator).

**Organization**: Tasks are grouped by user story. Two tasks are already complete in the working
tree (marked `[X]`) — they are the retrospective half of the spec, restored from commit `80213c09`
before the spec was written.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: US1, US2, US3 per spec.md
- Exact file paths included

## Path Conventions

Embedded firmware, single tree. Everything here lives in
`src/activities/boot_sleep/SleepActivity.{h,cpp}`.

---

## Phase 1: Setup

**None.** No new file, target, dependency or tool. The branch, the two source files and the build
environments all exist.

---

## Phase 2: Foundational (Blocking Prerequisites)

**None.** The feature reuses `UITheme::drawCenteredWrappedText()`
([src/components/UITheme.cpp:145-181](../../src/components/UITheme.cpp)) and the format readers'
existing `getTitle()`/`getAuthor()`. Nothing has to be built before the user stories can start.

**Checkpoint**: user story work can begin immediately.

---

## Phase 3: User Story 1 - Recognise the sleeping device's book without cover art (Priority: P1) 🎯 MVP

**Goal**: A cover sleep screen names the book instead of falling through to the logo or wallpaper
when the book has no usable cover art.

**Independent test**: open a coverless EPUB, set Sleep Screen to Cover, sleep — the screen shows a
framed card with the title and author. See [quickstart.md](quickstart.md) steps 1-4.

- [X] T001 [US1] In `renderCoverSleepScreen()` in `src/activities/boot_sleep/SleepActivity.cpp`, copy `getTitle()` and `getAuthor()` into local `bookTitle`/`bookAuthor` strings inside each of the four format branches (XTC, TXT/Markdown, FB2, EPUB) before the book object leaves scope, and turn each `generateCoverBmp()` failure from an early `return` into a fall-through that leaves `coverBmpPath` empty (FR-002, FR-003, FR-004). A book-*load* failure still returns to the existing fall-back — no metadata is available (FR-005).
- [X] T002 [US1] Add `renderCoverStubSleepScreen(const std::string& title, const std::string& author)` to `src/activities/boot_sleep/SleepActivity.h` and implement it in `src/activities/boot_sleep/SleepActivity.cpp`: two nested `drawRect` frames at 30 px and 36 px inset, title at `pageHeight / 3` in `UI_12_FONT_ID` bold, author below it in `SMALL_FONT_ID`, screen inverted when `sleepScreenCoverFilter == INVERTED_BLACK_AND_WHITE`, one `displayBuffer(HalDisplay::HALF_REFRESH)` (FR-007, FR-008, FR-013, FR-014). Call it from the end of `renderCoverSleepScreen()` when the cover BMP attempt failed and `bookTitle` is non-empty (FR-001, FR-005, FR-006).
- [X] T003 [US1] In `renderCoverSleepScreen()` in `src/activities/boot_sleep/SleepActivity.cpp`, treat a title that is empty **or contains no non-whitespace character** as absent, so a whitespace-only title falls back instead of drawing an empty frame (FR-005, spec Edge Cases). One condition at the call site — do not add a string-trimming helper.

**Checkpoint**: US1 delivers the MVP on its own. Titles are single-line and ellipsised until US2.

---

## Phase 4: User Story 2 - Read the whole title of a long-titled book (Priority: P2)

**Goal**: Title and author wrap across lines instead of being cut off after one.

**Independent test**: a book whose title is far wider than the screen shows the title over multiple
lines, distinguishable from a book sharing its opening words. [quickstart.md](quickstart.md) step 4.

- [X] T004 [US2] In `renderCoverStubSleepScreen()` in `src/activities/boot_sleep/SleepActivity.cpp`, replace both `truncatedText()` + `drawCenteredText()` pairs with two `UITheme::drawCenteredWrappedText()` calls sharing a fixed divider, per [research.md](research.md) §2 (FR-009 to FR-012):
  - `const int textMargin = innerMargin + 20;` and `const int maxTextWidth = pageWidth - textMargin * 2;` (unchanged from T002).
  - `const int divider = pageHeight / 3 + renderer.getLineHeight(UI_12_FONT_ID);`
  - Title: `Rect{textMargin, innerMargin + 8, maxTextWidth, divider - (innerMargin + 8)}`, `maxLines = 3`, `EpdFontFamily::BOLD`, `TextVerticalAlignment::BOTTOM`.
  - Author (only when non-empty): `Rect{textMargin, divider + 12, maxTextWidth, pageHeight - innerMargin - 8 - (divider + 12)}`, `maxLines = 2`, `TextVerticalAlignment::TOP`.
  - `y` is the top of the line box (`drawText` adds the ascender), so a one-line title lands at `pageHeight / 3` and a one-line author at `titleY + lineHeight + 12` — **pixel-identical to T002's output for short texts**. Verify that before looking at long ones.
  - The helper clamps lines to `bounds.height / lineHeight`, which is what keeps text off the frame (FR-011, FR-012); the `maxLines` values are the typographic limit, the bounds are the safety net.

**Checkpoint**: US2 complete. FR-009 to FR-012 satisfied without new layout arithmetic.

---

## Phase 5: User Story 3 - The stub card obeys the reader's sleep-screen choices (Priority: P3)

**Goal**: The card matches the reader's cover filter, refresh and orientation expectations.

**Independent test**: with the Inverted filter, the card is light-on-dark and the screen updates
exactly once.

**No implementation tasks.** T002 already satisfies FR-013 (filter), FR-014 (single half refresh)
and FR-015 (the sleep path resets to portrait before the mode switch, at
[src/activities/boot_sleep/SleepActivity.cpp:370-377](../../src/activities/boot_sleep/SleepActivity.cpp)).
This story is verification only.

- [X] T005 [US3] Verify in the simulator: set `sleepScreenCoverFilter` to `2` (Inverted) in `fs_branches/<branch>/.crosspoint/settings.json`, capture a sleep with the coverless fixture per [quickstart.md](quickstart.md) step 3, and confirm the card is light-on-dark and the log shows one screen update (FR-013, FR-014, SC-004). Then sleep from a reader left in `LANDSCAPE_CW` and confirm the card is still upright (FR-015).

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T006 Run `./bin/clang-format-fix -g` over the two changed files, then the merge gates in order: `bin/run-tests`, `bin/run-tests --asan`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (Constitution "Development Workflow & Quality Gates"). Note macOS ASan rejects `ASAN_OPTIONS=detect_leaks=1` — omit it locally.
- [X] T007 Run the full [quickstart.md](quickstart.md) validation, including the fixture variations in step 4 (one-word title, ~300-character title, spaceless title, no `dc:creator`, Cyrillic/Arabic title, a title carrying a lone UTF-8 continuation byte and embedded control characters, and a 10 KB title — the last two cover FR-018/SC-006) and the must-not-change checks in step 5 (FR-001 to FR-015, SC-001 to SC-007). Restore the simulator card per step 6.
- [X] T008 Commit the two source files as one logical change with a semantic message (`feat: …`), via `.specify/scripts/bash/speckit-commit.sh implement -m "feat: <subject>" src/activities/boot_sleep/SleepActivity.cpp src/activities/boot_sleep/SleepActivity.h`. No AI attribution (Constitution Principle VII).
- [X] T009 Confirm the change builds for one S3 board as well — `pio run -e sticky` — since a change is only proven on the family it was built for (AGENTS.md). Expected to be a formality here: the path has no board conditionals.

---

## Dependencies & Execution Order

### Phase Dependencies

- Phases 1 and 2 are empty — user story work starts immediately.
- **US1 (Phase 3)**: T003 depends on T001/T002 being in the tree (they are).
- **US2 (Phase 4)**: T004 edits the function T002 created, so it depends on T002. It does **not**
  depend on T003 — different statement, same file.
- **US3 (Phase 5)**: verification only; depends on T002 (and is worth re-running after T004).
- **Polish (Phase 6)**: after all desired stories.

### Within-Story Notes

No tests to fail first (see the Tests note above). No models, no services, no endpoints — this is a
render path.

### Parallel Opportunities

Effectively none, and that is the point: T003 and T004 touch the same file and the same function's
neighbourhood, so they are one editing session, not two parallel tracks. T009 can run while T007 is
being eyeballed.

---

## Implementation Strategy

### MVP

US1 alone is shippable and is already in the working tree; only T003 is outstanding. A reader gets
the whole value of the feature — the device names the book — with single-line titles.

### Increment

US1 (finish T003) → US2 (T004) → verify (T005, T007) → gates (T006, T009) → commit (T008). One
commit for the lot: it is one logical change, and the constitution asks for one-defect-one-commit
granularity, not one-task-one-commit.

---

## Notes

- 9 tasks, 2 already complete. Four are verification or gates rather than code.
- The entire code delta lives in one function plus one header line. If it starts growing a helper
  class, a new file or a setting, stop and re-read [research.md](research.md).
- Do not add a test for the `coverReadable ? art : (title.empty() ? fallback : stub)` branch; that
  decision is recorded and justified in the plan's Constitution Check.

---

## Phase 7: Convergence

Both items are evidence gaps, not code gaps: the branches exist and are correct by inspection,
but neither has been seen on screen. No source file should need to change.

- [ ] T010 Capture a sleep where the book's cached `cover.bmp` exists but is corrupt (truncate or scribble over `fs_branches/<branch>/.crosspoint/epub_<hash>/cover.bmp` for a book that has cover art), and confirm the stub card is drawn rather than the generic sleep screen, per US1/AC3 (partial). Every capture so far reached the stub via `generateCoverBmp()` failing, never via `Bitmap::parseHeaders()` rejecting a present file.
- [ ] T011 Capture a stub card whose author name is wider than one line, and confirm the author wraps within its own 2-line budget, stays inside the inner frame and keeps the title-to-author gap, per FR-010 and US2/AC3 (partial). Use a coverless EPUB fixture with a long `dc:creator`; the two-line author branch has never rendered.
