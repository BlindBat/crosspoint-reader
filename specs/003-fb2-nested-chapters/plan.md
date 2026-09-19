# Implementation Plan: FB2 Nested Chapter Navigation

**Branch**: `feature/fb2-nested-chapters` | **Date**: 2026-09-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-fb2-nested-chapters/spec.md`

## Summary

Make every `<section>` of an FB2 reading body a chapter. Both FB2 parsers currently
count only depth-1 sections on purpose — `Fb2MetadataParser.cpp:97-103` and
`Fb2SectionParser.cpp:120-136` — so nested stories are invisible and their parent is
one enormous chapter.

The approach changes one rule in each parser and deletes more state than it adds:

1. **Counting**: both parsers number *every* section inside a reading body, in
   document-start order. The two numberings stay identical because both walk the same
   bytes with the same body-exclusion rule.
2. **Extent**: a chapter's text is its section's **own direct content**; child-section
   subtrees are skipped because they are chapters in their own right. Nothing is
   duplicated and nothing is dropped.
3. **Hierarchy**: each chapter records its nesting `level`, and the chapter list indents
   by it exactly as the EPUB list already does
   (`EpubReaderChapterSelectionActivity.cpp:65`).
4. **TOC collapse**: with one chapter per section, `Fb2`'s parallel `tocEntries` vector
   becomes a duplicate of `sections` (it stores every title twice today). It is deleted;
   the TOC accessors project `sections` 1:1. Flat books end up using *less* RAM than
   before.
5. **Progress**: a chapter's `length` becomes its own byte span minus its child
   sections' spans, so chapter lengths still partition the reading body and the existing
   size-proportional progress arithmetic keeps working unchanged.
6. **Position migration**: `progress.bin` gains a trailing marker word. A payload
   without the marker is by definition pre-change, and its section index is read as a
   *top-level ordinal* — resolved to the first chapter of that part, page 0. Correct for
   flat books too (there the ordinal equals the index).

No new task, no new activity, no new UI component, no new dependency.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), `-fno-exceptions`, no RTTI

**Primary Dependencies**: in-tree expat (`XML_GE=0`, `XML_CONTEXT_BYTES=1024`),
`HalStorage`, `GfxRenderer`, the shared EPUB text pipeline (`ParsedText`/`Page`/`BlockStyle`)

**Storage**: SD card via `HalStorage`. Per-book cache under
`.crosspoint/fb2_<hash>/{book.bin, progress.bin, sections/<index>.bin}`

**Testing**: host gtest via `bin/run-tests` (plain + `--asan`). Existing suites touched:
`fb2_metadata_parser`, `fb2_section_parser`, `fb2_book`, `fb2_section_cache`,
`xtc_fb2_readers`. Fixtures in `test/fb2/`.

**Target Platform**: ESP32-C3 (`default`, ~380 KB RAM, no PSRAM) and the ESP32-S3 boards;
verification in crosspoint-simulator

**Project Type**: e-reader firmware — portable reader core in `lib/`, UI in `src/activities/`

**Performance Goals**: no regression in time-to-first-page; for the reference book the
largest chapter's layout work drops from 853 KB to 67 KB of source text, so opening a
story is expected to get faster, not slower. No timing assertion is claimed as a gate.

**Constraints**: chapter metadata for a whole book lives in RAM (it already does), so the
chapter count must be capped and titles bounded; total FB2 metadata RAM must not regress
for flat books and must stay bounded for nested ones.

**Scale/Scope**: reference book 1.9 MB, 66 chapters at 2 levels; cap at 1024 chapters.
Touched: 6 production files, 5 test suites, ~4 new fixtures.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Assessment |
|-----------|------------|
| **I. A Focused Reading Device** | PASS — this is core reading navigation for a format the firmware already supports, fixing a defect that makes most FB2 anthologies unnavigable. No new feature surface, no connectivity, no new theme. |
| **II. Memory Is the Design Constraint** | PASS with a measured trade. Deleting `tocEntries` removes one `std::string` copy of every chapter title, so flat books use less RAM after this change. Nested books hold more chapters (reference: 4 → 66 ≈ +5 KB), bounded by `FB2_MAX_CHAPTERS = 1024` and a per-title cap. The parser stack is a `std::vector` with `reserve()` before any `push_back`. No bare `new`; new buffers use `makeUniqueNoThrow`. No new allocation in a render-loop path. The RAM claim is measured, not asserted: tasks.md T041 checks it with the `AllocCounter` scope the `fb2_book` suite already compiles. |
| **III. Portability Behind the HAL** | PASS — all logic lands in `lib/Fb2/` (host-compilable, already covered by host suites) plus one pure-arithmetic change in `src/activities/reader/Fb2ReaderMath.*`, which exists precisely to be host-testable. Storage stays behind `HalStorage`. |
| **IV. Evidence Over Claims** | PASS — every number in spec and plan is measured from the reference file (1,915,806 bytes; 66 sections; largest 67,209 bytes) or cited to a file and line. The performance expectation is stated as a mechanism (less source text laid out per chapter), and the on-screen result is verified in the simulator. |
| **V. Tests Prove Behavior** | PASS — four existing tests pin the current top-level-only behaviour (`Fb2MetadataParserTest.cpp:100`, `Fb2SectionParserTest.cpp:434,448,457`). Each is rewritten to the new contract and MUST be shown failing against unmodified production code before the fix lands. New fixtures cover the shapes the change creates: deep nesting, a title-only parent, a content-only wrapper, trailing parent content, and the chapter cap. |
| **VI. Untrusted Input Is Hostile** | PASS — `book.bin` gains a `level` field and a new version, so every count and length is re-validated against physical file size before any `reserve()`; chapter count is capped; `progress.bin` payload lengths are validated as they are today; malformed nesting is bounded by the cap and by expat's own well-formedness checks. Corpus additions: unbalanced sections, 64-deep nesting, a cache claiming 65535 chapters. |
| **VII. Upstream-First Fork Hygiene** | PASS — one defect, one branch (`feature/fb2-nested-chapters`), cherry-pickable commits, no AI co-authors. Diff stays well under 200 lines of non-test change. Upstream has no FB2 chapter work in flight to mirror (to be re-checked before any upstream PR); host suites and fixtures stay fork-only. |

**Gate result**: PASS, no violations to justify. See Complexity Tracking for the one
deliberate simplification (chapter cap) and the architecture deliberately *not* adopted
(SD-resident chapter metadata).

### Post-design re-check (after Phase 1)

Re-evaluated against the Phase 1 artifacts; still PASS. Three obligations the design
surfaced, all folded into the contracts:

- **Principle VI**: `book.bin` v3 adds a `level` byte, so `FB2_CACHE_MIN_SECTION_ENTRY`
  must grow with it and the chapter count must be checked against both
  `FB2_MAX_CHAPTERS` and the remaining file size before `reserve()`; the level sequence is
  itself validated (`level <= previous + 1`). See
  [contracts/file-formats.md](contracts/file-formats.md).
- **Principle VI / III**: the 8-byte `progress.bin` payload is FB2-local. XTC and TXT
  share `ProgressFile::writeAtomic` but not the payload decoder, so their formats stay at
  6 bytes and their suites must keep passing untouched.
- **Documentation**: `docs/file-formats.md` already documents FB2 `book.bin`, the section
  file and the 6-byte FB2 progress payload (lines 19, 651, 691-724); all three entries are
  updated in the same commit as the version bumps, per AGENTS.md's cache-versioning rule.

## Project Structure

### Documentation (this feature)

```text
specs/003-fb2-nested-chapters/
├── plan.md              # This file
├── research.md          # Phase 0: decisions and rejected alternatives
├── data-model.md        # Phase 1: chapter model, cache layouts, invariants
├── quickstart.md        # Phase 1: how to validate the feature end to end
├── contracts/
│   ├── chapter-model.md # Parser numbering + extent rules (the core contract)
│   └── file-formats.md  # book.bin v3, sections/<n>.bin v5, progress.bin v2
├── checklists/
│   └── requirements.md  # Spec quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
lib/Fb2/
├── Fb2.h                     # SectionInfo gains `level`; tocEntries deleted;
├── Fb2.cpp                   #   cache v3 read/write, caps, ordinal→index helper
└── Fb2/
    ├── Fb2MetadataParser.h    # section stack replaces the depth==1 filter
    ├── Fb2MetadataParser.cpp  #   own-title, own-length, level, chapter cap
    ├── Fb2SectionParser.h     # counts every section; suppresses child subtrees
    ├── Fb2SectionParser.cpp   #   + empty-chapter guard (one blank page)
    └── Fb2Section.cpp         # FB2_SECTION_FILE_VERSION 4 → 5

src/activities/reader/
├── Fb2ReaderChapterSelectionActivity.cpp  # indent rows by level (EPUB convention)
├── Fb2ReaderMath.h / .cpp                 # progress.bin v2 marker + legacy decode
└── Fb2ReaderActivity.cpp                  # remap a legacy ordinal on load

test/
├── fb2/                      # new fixtures: deep-nesting, wrapper-only,
│                             #   title-only-parent, trailing-parent-content
├── fb2_metadata_parser/      # flipped pin + numbering/level/length suites
├── fb2_section_parser/       # flipped pins + subtree-suppression suites
├── fb2_book/                 # cache v3 round-trip, caps, ordinal mapping
├── fb2_section_cache/        # version bump + empty-chapter page guard
├── xtc_fb2_readers/          # progress.bin v2 encode/decode + legacy path
└── corpus/                   # malformed nesting additions
```

**Structure Decision**: unchanged from the repo's existing split — format logic in
`lib/Fb2/` (host-compilable, Principle III), reader/UI behaviour in
`src/activities/reader/`, host tests mirroring each. This feature adds no directory.

## Complexity Tracking

> No Constitution violations. Recorded here: the one capped simplification and the
> heavier design that was rejected.

| Decision | Why | Simpler / other alternative rejected because |
|----------|-----|----------------------------------------------|
| `FB2_MAX_CHAPTERS = 1024`; once reached, further nested sections render as part of their containing chapter | Chapter metadata is RAM-resident, so the count must be bounded to satisfy Principle II and FR-013. Degrading to "no further splitting" loses no text and needs one comparison in each parser | Dropping sections past the cap would lose text (violates FR-003); refusing the book punishes the reader for a large file; an unbounded vector is an OOM waiting on a 380 KB device |
| Chapter metadata stays in RAM (`std::vector<SectionInfo>`) | It already does today; keeping it avoids a new binary cache layout, a LUT, and a seek path for a defect fix | Moving FB2 chapter metadata to SD with a seekable LUT (as `BookMetadataCache` does for EPUB) is the architecturally right answer for books with thousands of sections and would remove the cap — but it is a format-and-cache redesign, several times this diff, and belongs in its own spec. Marked as the upgrade path in code with a `ponytail:` comment |
| Parent-section text that follows a child section is read with the parent's chapter, ahead of the children | Keeps the rule "a chapter is a section's own direct content" to one sentence, and guarantees no text is lost | Splitting the parent into pre-child and post-child chapters adds an untitled chapter to the list for a shape that is rare in real files; dropping the trailing text violates FR-003. The reordering is the documented ceiling (`ponytail:` comment, and an explicit test) |
