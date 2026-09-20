# Implementation Plan: FB2 Reader Hardening

**Branch**: `feature/fb2-reader-hardening` | **Date**: 2026-09-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/004-fb2-reader-hardening/spec.md`

## Summary

Four of the fork's seven open issues, in one pass over FB2: bound the chapter ceiling so a large
book cannot exhaust the C3 (#6), window the chapter list so its memory stops scaling with
chapter count (#7), show the already-extracted cover as the book's first page (#5), and label
untitled sections from their own first line instead of `Unnamed` (#10).

The approach is four small, independent changes — none of them a new subsystem:

1. `FB2_MAX_CHAPTERS` 1024 → **256**, one constant, no board gating, chosen from a measured
   corpus of 2,899 real FB2 books rather than a round number: at 1024 the worst real book costs
   **99,716 B — 44.9% of the 222,180 B of heap the C3 has left after its framebuffer**; at 256 it
   costs 43,732 B (19.7%) and 22 books (0.76%) get coarser navigation without losing a byte of
   text. The existing cache validation already rejects and rebuilds anything above the ceiling,
   so lowering it needs no new code path.
2. The FB2 chapter list adopts the **24-row window** `EpubReaderChapterSelectionActivity`
   already runs on `fui::ListProps::itemsWindowFirst` — reuse, not new machinery.
3. `Fb2MetadataParser` derives a **bounded first-paragraph label** for untitled sections while
   it is already parsing, marked by a `flags` byte in `book.bin` v4. Expat is what makes the
   text correct: FB2 is routinely windows-1251, and a display-time derivation from raw offsets
   would be mojibake plus an SD read per visible row.
4. The cover is **reader state, not a page**: one `bool` in `Fb2ReaderActivity`, drawn over the
   loaded section. No chapter index, page number or cached page changes, so no reading position
   moves and no `sections/<n>.bin` is invalidated.

Full reasoning and rejected alternatives: [research.md](research.md).

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), no exceptions, no RTTI

**Primary Dependencies**: expat (`XML_GE=0`, `XML_CONTEXT_BYTES=1024`), SdFat behind
`HalStorage`, `GfxRenderer`, `UITheme`/`UiListActivity`, `lib/Utf8`, `lib/I18n`

**Storage**: SD card only — `/.crosspoint/fb2_<hash>/{book.bin, sections/<n>.bin, progress.bin,
cover.bmp}`; SPIFFS is not mounted

**Testing**: host gtest via `bin/run-tests` (plain and `--asan`); simulator for the two
rendering changes; hardware for heap figures

**Target Platform**: ESP32-C3 (X4/X3, ~380 KB usable RAM, no PSRAM) is the sizing target;
ESP32-S3 boards `sticky`, `x4pro`, `x4c`, `papermono` build the same logic

**Project Type**: embedded firmware — portable reader core in `lib/`, UI in `src/activities/`,
hardware behind `lib/hal/`

**Performance Goals**: no regression in chapter-list scroll or page-turn latency; one extra
expat metadata pass per FB2 book, once, on first open after the update

**Constraints**: no real book's chapter metadata may exceed ~20% of the 222,180 B of measured
usable heap; chapter-list memory constant in chapter count; no `sections/<n>.bin` invalidation;
no reading position moved

**Scale/Scope**: ~4 files in `lib/Fb2`, 2 activity files, `docs/file-formats.md`, one i18n key,
and the `fb2_book` / `fb2_metadata_parser` host suites

## Constitution Check

*GATE: evaluated before Phase 0 and re-evaluated after Phase 1 design. Result below is the
post-design evaluation.*

| Principle | Verdict |
|---|---|
| **I. A Focused Reading Device** | **PASS.** Every item is the core reading experience: a book that opens, a list you can read, the cover of the book you are reading. No new connectivity, no new theme, nothing outside SCOPE.md. Issue #4 (background prefetch), the one item that would need a Discussion under this principle, is out of scope. |
| **II. Memory Is the Design Constraint** | **PASS, and it is the point.** Measured, not asserted: the worst real book's chapter metadata drops from **99,716 B to 43,732 B**, against a measured budget of 222,180 B (327,680 DRAM − 56,348 static from `pio run -e default` − 48 KB framebuffer) — 44.9% of the reader's heap down to 19.7%. Mechanism: the vector shrinks 4× with the ceiling, 256 entries × `sizeof(SectionInfo)` = 36 B read out of a riscv32 object file. Chapter-list memory becomes `24 × (std::string + ListItem)` instead of `n ×`. Added cost: one `bool` and one `std::string` in the reader, one `uint8_t` per chapter on disk — and the chapter's `uint8_t` is free, since the flag packs into `SectionInfo`'s existing padding (`sizeof` stays 36 B). No bare `new`; the window arrays are fixed-size members, not vectors. |
| **III. Portability Behind the HAL** | **PASS.** Label derivation, the ceiling and the cache format live in `lib/Fb2` and stay host-compilable — that is how L1–L8 are tested. The cover page touches `GfxRenderer` and a `Bitmap` from `HalStorage`, both already the seam the sleep screen uses; no Arduino include crosses into `lib/`. |
| **IV. Evidence Over Claims** | **PASS.** The ceiling is chosen from three measurements, each reproducible and recorded in research.md (M1–M3): the device's real static-RAM figure from a firmware build; `sizeof` values read out of a riscv32 object file rather than assumed; and 2,899 real FB2 books parsed through this repo's own `Fb2MetadataParser` (not a grep for `<section`, which overstates counts by ~40% because it counts notes bodies). The per-chapter cost model is stated as a formula so it can be rechecked. Host allocation figures from `Fb2BookTest.cpp:340-382` remain the regression proxy. The dithered-cover appearance and live heap pressure stay deferred to device verification. |
| **V. Tests Prove Behavior** | **PASS with obligations.** L1–L8 and the v4 round trip land with their tests in the same commits, using the existing `fb2test` fixture generators and `AllocCounter` scopes. The retuned allocation budgets must fail against the old ceiling, and the new v4 validation cases must fail against a reader that skips the `flags` checks — each new suite owes its demonstrated mutation. V1–V7 are not host-reachable; they are simulator/device checks in [quickstart.md](quickstart.md) and are labelled as such rather than faked with a host stub. |
| **VI. Untrusted Input Is Hostile** | **PASS.** The new `flags` byte is validated before use (`flags & ~0x01`, and derived-implies-non-empty), the count check keeps running before `reserve()` with `FB2_CACHE_MIN_SECTION_ENTRY` grown by one, and the derived-label cap bounds the one string this feature lets a book's prose write into the cache. `cover.bmp` is opened through `HalStorage` and rejected on a header parse failure — the sleep screen's existing path, which V2 makes explicit. Lowering the ceiling *tightens* an existing validation rather than adding a new trust. |
| **VII. Upstream-First Fork Hygiene** | **PASS.** Work is on `feature/fb2-reader-hardening`, not `master`. The four changes are separable into one-defect-one-commit cherry-picks: the ceiling, the window, the labels (+v4), the cover page. Host tests stay fork-only and out of any upstream PR branch. Each issue is a fork issue with no upstream PR open against it — verified at spec time. |

**No violations requiring justification.** One spec-level deviation is recorded below.

## Project Structure

### Documentation (this feature)

```text
specs/004-fb2-reader-hardening/
├── plan.md                        # This file
├── spec.md
├── research.md                    # Phase 0: the six decisions
├── data-model.md                  # Phase 1: entities and state transitions
├── quickstart.md                  # Phase 1: how to validate
├── contracts/
│   ├── file-formats.md            # book.bin v4 layout + validation table
│   └── reader-behaviour.md        # L1–L8 (labels), V1–V7 (cover page)
├── checklists/requirements.md
└── tasks.md                       # /speckit-tasks output — NOT created here
```

### Source Code (repository root)

```text
lib/Fb2/
├── Fb2.h                          # FB2_MAX_CHAPTERS 1024 -> 256; FB2_MAX_LABEL_CHARS;
│                                  # SectionInfo.titleDerived (packs free into padding)
├── Fb2.cpp                        # FB2_CACHE_VERSION 3 -> 4; flags read/write + validation;
│                                  # FB2_CACHE_MIN_SECTION_ENTRY += 1
└── Fb2/
    └── Fb2MetadataParser.{h,cpp}  # first-paragraph label derivation + its cap (L1-L8)

src/activities/reader/
├── Fb2ReaderChapterSelectionActivity.{h,cpp}   # 24-row window; derived-label rendering
└── Fb2ReaderActivity.{h,cpp}                   # onCoverPage state + cover draw (V1-V7)

lib/I18n/translations/*.yaml       # one new key: the derived-label format string
docs/file-formats.md               # book.bin v4, the new ceiling
specs/003-fb2-nested-chapters/     # ceiling value mirrored where 003 documents it (FR-007)

test/fb2_book/Fb2BookTest.cpp                   # retuned budgets, v4 round trip, flags corruption
test/fb2_metadata_parser/                       # L1-L8
```

**Structure Decision**: existing layout, no new directories. Format, ceiling and labels are
reader-core work in `lib/Fb2` (host-testable per Principle III); the window and the cover page
are UI work in `src/activities/reader`. Nothing new is introduced under `lib/hal`.

## Complexity Tracking

> No constitution violations. One spec-level deviation, recorded so `/speckit-analyze` sees it
> rather than flagging it as drift.

| Item | Why needed | Simpler alternative rejected because |
|---|---|---|
| `book.bin` v4 bump | The derived-label marker must survive the cache round trip or a book shows different labels on its first and second open. The rebuild is one expat metadata pass per book, on first open after the update — it does not re-paginate anything (verified: `Fb2::load()` falls back to `parseMetadata()` alone, `lib/Fb2/Fb2.cpp:186-206`). **SC-006 was amended in the spec to match**: no page cache is invalidated and no reading position is lost | Not bumping means every book already on the device keeps showing `Unnamed` until something unrelated rebuilds its metadata — FR-015 satisfied only for books added after the update. A sidecar flags file means a second file to open, validate and keep in sync to carry one bit per chapter |
| A `flags` byte rather than no distinguishability at all (FR-016) | A derived label is the book's prose standing in for a title; a reader who cannot tell them apart will read a first sentence as a chapter name | Dropping FR-016 removes the byte *and* the version bump above. It is the cheapest thing to cut if the bump is judged not worth it — flagged here as the single decision that unlocks that simplification |
| `FB2_MAX_LABEL_CHARS`, a second new constant, applied to **derived labels only** | A derived label is prose — a `<p>` can be a whole paragraph, so without a cap one untitled section stores kilobytes. FR-018 needs it | Capping **real** titles too was in the first draft and the corpus killed it: it moves the worst book from 99,716 B to 96,668 B (3%), because only 13.6% of 85,128 real chapters have a title over 64 characters and the 36 B struct dominates. Not worth changing what the reader sees in 34% of books |
| Ceiling **256** rather than a byte budget | A count is what `book.bin` stores and validates (`u16 chapterCount`, `countFitsRemainingFile`), so a count cap is one comparison in code that already exists | A byte budget would bound the actual resource and adapt to title length — measured, books at 256 chapters span 9 KB to 44 KB of metadata, a 5× spread, so a count is a weak proxy. It still needs a count bound for cache validation, so it adds a constant instead of replacing one. Recorded as the upgrade path if issue #8 is deferred again |

## Deliberate simplifications (each with its upgrade path)

- **Grayscale cover.** The cover page draws in black and white; the sleep screen's three-pass
  grayscale pipeline is skipped. Upgrade if a dithered cover reads poorly on device.
- **No glyph prewarm on the FB2 chapter list.** EPUB batch-prewarms fallback glyphs after each
  window refresh because its TOC entries are SD reads; FB2's are in RAM. Upgrade if CJK FB2
  lists repaint slowly.
- **One ceiling for every board.** FR-002 permits a board-dependent value; a single constant is
  taken instead — two ceilings mean the same SD card carries caches one board rejects, to serve
  22 books out of 2,899. Upgrade path is issue #8's SD-resident LUT, which deletes the constant
  entirely — not a second constant.
- **The ceiling degrades 22 real books, and not gently.** Past the ceiling a book's tail
  collapses into one chapter: measured, a degraded book puts a median 31.5% and a worst 77.2% of
  its text into that chapter, which is a pagination cost, not just coarser navigation. Two books
  in the corpus already hit today's 1024 cap, so this is a widening of existing behaviour rather
  than a new failure mode — but it is the strongest measured argument for issue #8.

These belong in the code as `ponytail:` comments naming the ceiling and the upgrade path, as
`003` did.
