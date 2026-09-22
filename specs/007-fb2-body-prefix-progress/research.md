# Phase 0 Research: FB2 Body-Level Front Matter Carries Progress Weight

All findings are from the tree at `feature/fb2-body-prefix-progress` and from the
2,899-book FB2 corpus (`~/Calibre Library`), scanned 2026-09-22.

## R1 — Where the weight is lost

`Fb2MetadataParser` opens a section and records `open.startOffset =
XML_GetCurrentByteIndex()` ([lib/Fb2/Fb2/Fb2MetadataParser.cpp:139-141](../../lib/Fb2/Fb2/Fb2MetadataParser.cpp)).
On `</section>` it computes `span = endOffset - closed.startOffset`, subtracts the child
chapters' spans, and stores the remainder as the chapter's `length`
([Fb2MetadataParser.cpp:268-283](../../lib/Fb2/Fb2/Fb2MetadataParser.cpp)). `Fb2::calculateProgress`
then blends `cumulativeLength - length` with `chapterRead * length` over `bookSize`
([lib/Fb2/Fb2.cpp:582-589](../../lib/Fb2/Fb2.cpp)), where `bookSize` is the last chapter's
`cumulativeLength` ([Fb2.h:47](../../lib/Fb2/Fb2.h)).

So the weighted universe is exactly "bytes inside a top-level `<section>` of a reading
body". Everything before a body's first `<section>` is outside every span. It is still
*rendered*, by `Fb2SectionParser`'s `inBodyPrefix` flag, into the body's first chapter
([Fb2SectionParser.cpp:118-141](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)) — contract rule C10.

**Decision**: the defect is one missing byte range, not a modelling error. Fix it where the
span is decided.

## R2 — Approach: extend the first chapter's span, don't add a chapter

**Decision**: when a `<body>` becomes a reading body, record its start byte. When that
body's **first** `<section>` opens, start its span at the body's start instead of its own
tag, keeping `info.fileOffset` pointing at the `<section>` tag. Clamp: use the body offset
only when it is strictly below the section offset.

**Rationale**: it is the smallest change that makes weight and display agree, because it
reuses the span arithmetic that already exists. `startOffset` and `info.fileOffset` are
already separate fields with separate jobs — `startOffset` feeds the span,
`info.fileOffset` is persisted and used only as a `== 0` sentinel for the whole-file
fallback ([Fb2Section.cpp:215-217](../../lib/Fb2/Fb2/Fb2Section.cpp)) — so widening one does
not disturb the other. The body's first section is always top-level (`openSections` is
empty at that point and is cleared at every `</body>`), so no parent's `childBytes` is
affected and the partition stays exact.

**Alternatives considered**:

- *Give the front matter its own chapter*: rejected in issue #9 and again here — it would
  add a chapter-list row to every book that has a body, and change chapter numbering, which
  would invalidate every persisted reading position.
- *Add the prefix to `info.length` after the section closes*: same result, but needs the
  prefix carried as a second piece of state and re-applied on the unbalanced-markup path.
  More state, no benefit.
- *Weight by rendered characters instead of source bytes*: would fix this and the markup-
  overhead skew at once, but needs a full render pass to compute and a new cache field.
  Out of proportion to a percentage display.

## R3 — Cache invalidation, and the relayout trap

`book.bin` holds `ownLength` and `cumulativeLength` per 20-byte record
([Fb2.cpp:43-53](../../lib/Fb2/Fb2.cpp)). The record *layout* does not change; the *values*
do, so a v5 cache read by the new code would report old weights with no symptom.

**Decision**: bump `FB2_CACHE_VERSION` 5 → 6.

**Trap found**: `Fb2::load` drops the whole `sections/` directory when a rejected cache is
older than the current version *and* the book has more than `FB2_OLD_CHAPTER_CAP` (256)
chapters ([Fb2.cpp:384-387](../../lib/Fb2/Fb2.cpp)). That clause exists because layouts
built under the old 256-chapter cap are stale, which was true of caches older than v5 — not
of v5 itself. Left as written, the bump would force a full relayout of every >256-chapter
book for no reason; specs/006 research R10 measured the metadata rebuild of a 677-chapter
book at 2.9 s, and a relayout is far more than that.

**Decision**: pin that clause to the version that removed the cap (`rejectedVersion < 5`)
via a named constant, so v5 → v6 rebuilds `book.bin` only. `sections/` and `progress.bin`
survive, which is correct: page layout and chapter numbering are unchanged by this feature.

## R4 — Reading position is safe

FB2 progress is persisted as `sectionIndex` + `page` + `pageCount` in `progress.bin`
([Fb2ReaderActivity.cpp:94-120](../../src/activities/reader/Fb2ReaderActivity.cpp)), never as
a percentage. Chapter numbering does not change, `progress.bin` is not touched by a
`book.bin` rebuild, and `sections/` is kept (R3). FR-010 therefore holds by construction,
and the quickstart verifies it rather than assuming it.

**Correction to a spec assumption**: `lib/KOReaderSync/` contains no FB2 path at all
(`ProgressMapper` is EPUB-only, [ProgressMapper.cpp:837](../../lib/KOReaderSync/ProgressMapper.cpp)).
The spec's assumption that KOReader "follows automatically" is vacuously true — there is
nothing to follow. No work, no risk.

## R5 — Corpus measurement (Constitution IV)

Scanned with a Python expat pass mirroring the parser's own offset arithmetic (section span
minus child spans; reading bodies = body 1 plus later unnamed bodies):

| Measurement | Value |
|---|---|
| Files scanned | 2,899 (2,889 with weighted content) |
| Front matter > 200 B | 380 books (13.1%) |
| Front matter ≥ 0.5 displayed percentage point | 165 books (5.7%) |
| Front matter ≥ 1.0 point | 55 books (1.9%) |
| Worst case | 256,971 B front matter vs 403,143 B sections = 38.9% of the book |
| Body-level text *after* the first section | ≤ 1,779 B; 90 books (3.1%) over 200 B |
| Reading bodies with no `<section>` | 10 books |

The worst case (`Moie tielo - Bosfor`) is a whole novel written as direct `<body><p>` with
a trailing afterword section — not a malformed file. This is what overturns issue #9's own
"probably never worth fixing".

## R6 — Tests that must move, and the oracle

Three existing assertions encode the old rule and must be restated, not deleted:

- `Fb2MetadataParserTest.SectionOffsetsPointAtTheSectionTags:56` asserts
  `fileOffset + length == </section> + 10` for every chapter. True only for chapters that
  are not their body's first. Restate: chapter 0's span starts at its body's `<body>` tag.
- `fb2test::topLevelSectionBytes` ([Fb2TestSupport.h:156-177](../../test/fb2_common/Fb2TestSupport.h))
  is the independent oracle for "chapter lengths partition the body", used by three suites
  ([Fb2BookTest.cpp:312,425,502](../../test/fb2_book/Fb2BookTest.cpp),
  [Fb2MetadataParserTest.cpp:213](../../test/fb2_metadata_parser/Fb2MetadataParserTest.cpp)).
  It must count each reading body from its `<body>` tag, staying an independent
  re-derivation from the source text rather than a copy of the parser.
- `Fb2SectionGoldenTest`'s page hashes for `body-prefix.fb2` must **not** change — that is
  the machine-checkable form of FR-003.

**Decision**: `test/fb2/body-prefix.fb2` is the pin fixture; it already exists and already
covers the rendering half of C10. Its exact numbers: body at 188, first `<section>` at 314,
so 126 bytes of front matter; chapter 0's weight 110 → 236 B, book weight 223 → 349 B,
chapter 0's share 49.3% → 67.6%. A pin asserting 236/349 fails against the unfixed parser,
satisfying Constitution V's mutation requirement without a contrived mutant.

**Gap**: no fixture has two *reading* bodies (`notes-body.fb2` is the named/auxiliary case).
FR-006 needs one: a second `<body>` with no `name`, carrying its own front matter.

## R7 — Cost

No allocation, no extra pass, no extra file I/O: one `size_t` member on a parser that is
already resident, set once per body and read once per body. Flash cost is a ternary and an
assignment. The only runtime cost is the one-off `book.bin` rebuild from R3, which is the
same parse every first open already performs.
