# Implementation Plan: FB2 Body-Level Front Matter Carries Progress Weight

**Branch**: `feature/fb2-body-prefix-progress` | **Date**: 2026-09-22 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/007-fb2-body-prefix-progress/spec.md`

## Summary

A reading body's bytes ahead of its first `<section>` are rendered into that body's first
chapter but counted in no chapter's weight, so the progress percentage freezes while they
are read. The fix is to start the body's first chapter's byte span at the `<body>` tag
instead of at its own `<section>` tag, keeping `fileOffset` — and therefore everything that
renders — exactly where it is. One parser member, one clamped assignment, a `book.bin`
version bump, and the three existing assertions that encode the old rule restated.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), no exceptions, no RTTI

**Primary Dependencies**: expat (in-tree, `XML_GE=0`, `XML_CONTEXT_BYTES=1024`); no new dependency

**Storage**: SD card via `HalStorage`; `.crosspoint/fb2_<hash>/book.bin` (v5 → v6), `sections/`, `progress.bin`

**Testing**: host gtest via `bin/run-tests`, plain and `--asan`; suites `fb2_metadata_parser`, `fb2_book`, `fb2_section_cache`, `fb2_section_parser`

**Target Platform**: ESP32-C3 (~380 KB RAM, single core) and the S3 boards; all touched code is host-compilable reader core

**Project Type**: embedded firmware library (`lib/Fb2`) with host test program

**Performance Goals**: no measurable change to parse or render time; no new pass over the file

**Constraints**: zero new heap allocation; one-off `book.bin` rebuild only (no `sections/` relayout — see research R3); rendered output byte-identical

**Scale/Scope**: ~10 lines of production change across 2 files, plus tests, one new fixture, and a contract amendment

## Constitution Check

*GATE: passed before Phase 0, re-checked after Phase 1.*

| Principle | Assessment |
|---|---|
| **I. A Focused Reading Device** | Passes (c): the progress percentage is core reading UX, and it is currently wrong by up to 38.9% of a book. No new surface, no new setting. |
| **II. Memory Is the Design Constraint** | Passes: one `size_t` member on an already-resident parser. No allocation, no `std::vector` growth, no new string. Flash cost is a ternary. |
| **III. Portability Behind the HAL** | Passes: `Fb2MetadataParser` and `Fb2` are pure reader core, already host-compilable; no Arduino/hardware include is added. |
| **IV. Evidence Over Claims** | Passes: every number in the spec comes from the 2,899-book corpus scan (research R5); the 2.9 s relayout figure is cited from specs/006 R10, not invented. No performance claim is made beyond "no new pass". |
| **V. Tests Prove Behavior** | Passes: pin test on `body-prefix.fb2` asserts 236/349 B, which the unfixed parser cannot produce (research R6); the golden page hashes pin FR-003; a new two-reading-body fixture covers FR-006. |
| **VI. Untrusted Input Is Hostile** | Passes: the body offset is used only when strictly below the section offset, so a hostile or unavailable byte index degrades to today's behaviour instead of underflowing a `size_t`. Malformed fixtures already in `test/fb2/` are re-run under ASan. |
| **VII. Upstream-First Fork Hygiene** | Passes: single-defect change on a short-lived branch cut for it, under 200 lines of non-test diff, cherry-pickable. Fork-only test suites stay out of any upstream branch. Issue #9 is the fork's own; no upstream PR overlaps `lib/Fb2` weight arithmetic. |

**Post-Phase-1 re-check**: unchanged. The design added no entity, no interface and no
allocation; the only artifact beyond code is a contract amendment to two existing rules.

## Project Structure

### Documentation (this feature)

```text
specs/007-fb2-body-prefix-progress/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   └── chapter-model.md # Phase 1 output — amends C7 and C10 of specs/003
├── checklists/
│   └── requirements.md  # From /speckit-specify
└── tasks.md             # /speckit-tasks output — NOT created here
```

### Source Code (repository root)

```text
lib/Fb2/
├── Fb2.cpp                      # FB2_CACHE_VERSION 5 -> 6; pin the cap-drop clause to v5
└── Fb2/
    ├── Fb2MetadataParser.h      # + bodyPrefixStart member
    └── Fb2MetadataParser.cpp    # record it on <body>; consume it on the body's first <section>

test/
├── fb2/
│   ├── body-prefix.fb2          # existing pin fixture (unchanged)
│   └── multi-reading-body.fb2   # NEW — two unnamed bodies, each with front matter
├── fb2_common/Fb2TestSupport.h  # topLevelSectionBytes oracle counts from <body>
├── fb2_metadata_parser/         # restate the offset assertion; add the weight pins
└── fb2_book/                    # progress pins over the amended partition
```

**Structure Decision**: no new file in `lib/`. The change lives entirely in the FB2
metadata parser and its cache version, which is where the byte span is decided (research
R1). Tests follow the existing one-suite-per-unit layout.

## Phase Outline

- **Phase A — Contract and oracle**: amend C7/C10 (this feature's `contracts/chapter-model.md`), teach `topLevelSectionBytes` to count each reading body from its `<body>` tag. The oracle moves first so the partition assertions describe the intended rule before the parser does.
- **Phase B — Pins that fail**: add the weight pins on `body-prefix.fb2` (chapter 0 = 236 B, book = 349 B) and the new two-reading-body fixture; restate `SectionOffsetsPointAtTheSectionTags`. Demonstrate they fail against the unfixed parser (Constitution V).
- **Phase C — Parser change**: `bodyPrefixStart` member, set on a reading `<body>`, consumed clamped by that body's first `<section>`, cleared at `</body>`.
- **Phase D — Cache**: `FB2_CACHE_VERSION` 5 → 6 and pin the `sections/`-drop clause to the pre-v5 cap era (research R3).
- **Phase E — Gates**: `bin/run-tests` plain and `--asan`, golden hashes unchanged, `pio run -e default`, `pio check`, `clang-format-fix -g`. Device check of a real front-matter book is the human step.

## Complexity Tracking

No Constitution Check violations. Table intentionally empty.
