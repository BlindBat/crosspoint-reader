# Phase 0 Research: FB2 Nested Chapter Navigation

All findings are from the in-tree code and from the reference book on the test card
(`fs_/Брэдбери Рэй/Марсианские хроники. Полное издание.fb2`, 1,915,806 bytes). No
NEEDS CLARIFICATION items remained after this pass.

## Baseline: why nested chapters are invisible

- `lib/Fb2/Fb2/Fb2MetadataParser.cpp:97-103` — a section is recorded only when
  `sectionDepth == 1`; the title is collected only when `sectionDepth == 1`
  (`:104-108`). Nested sections contribute neither a chapter nor a TOC entry.
- `lib/Fb2/Fb2/Fb2SectionParser.cpp:120-136` — the render parser counts only sections
  with `sectionNesting == 0`, and once inside the target section it renders **all**
  descendants. The comment states the intent: "Sections nested inside an earlier chapter
  are content of that chapter, not chapters of their own."
- Consequence on the reference book: reading body holds 66 sections, 4 of them top
  level, so the chapter list shows 4. Top-level extents measured: 922 B, 853,708 B,
  660,695 B, 312,340 B. A second body carries `name="notes"` and its 14 sections are
  correctly excluded (`hasNameAttribute` check, `Fb2MetadataParser.cpp:83-90`).

## Decision 1: every section is a chapter; a chapter is a section's own direct content

**Decision**: number every `<section>` inside a reading body in document-start order. A
chapter renders its section's own direct content and suppresses child-section subtrees,
because those are chapters of their own.

**Rationale**: it is one rule that satisfies FR-001 through FR-004 simultaneously — no
duplicated text (a parent no longer re-renders its children), no lost text (a parent's
own title page / epigraph keeps a home), and chapter extents that partition the body so
the existing progress arithmetic still sums to 100%.

**Alternatives considered**:

- *Leaf sections only are chapters* (63 chapters on the reference book). A parent's own
  front matter would have to be glued onto its first child, which either loses the
  parent's title as a list entry or invents a synthetic entry — more rules, not fewer.
- *Parent chapter stops at its first child*. Simplest to implement (one early stop), but
  it silently drops parent text that follows a child section — the in-tree fixture
  `test/fb2/nested-sections.fb2` has exactly that shape ("Outer closing paragraph."), so
  the defect would be immediate and FR-003 would fail.
- *TOC-only anchors into the existing big chapters*: the reader has no intra-chapter
  anchor concept (`Fb2Section` caches pages per chapter index, `Fb2Section.cpp:160-180`),
  so this is the largest of the three options and leaves the 853 KB layout cost in place.

**Ceiling accepted**: parent text after a child section reads with the parent, ahead of
the children, rather than in strict document position. Documented with a `ponytail:`
comment and pinned by a test.

## Decision 2: the two parsers must count identically, including inside suppressed subtrees

**Decision**: the render parser counts sections *before* it applies any content
suppression, so a nested chapter's own subtree still advances the counter.

**Rationale**: `Fb2SectionParser` has a single `skipUntilDepth` mechanism used for
`<description>`, `<binary>`, `<image>` and `<table>`, and it returns from `startElement`
**before** the `section` branch (`Fb2SectionParser.cpp:93-99`). Reusing it for nested
chapters would stop counting inside them, and the two numberings would silently diverge —
the reader would open the wrong chapter. Suppression therefore needs its own depth marker
that gates content handlers only.

**Alternatives considered**: reordering the section branch above the skip check — works,
but makes `<section>` special-cased against the generic skip contract and risks counting
sections inside `<description>`; a separate marker is clearer and equally small.

## Decision 3: chapter length = own span − child spans

**Decision**: the metadata parser keeps a small stack of open sections; on close, a
section's `length` is `(end − start) − sum(child spans)` and the full span is added to
its parent's child total. Entries are pushed at *start* time so vector order is document
order, and the length is filled in at close.

**Rationale**: `Fb2::calculateProgress` / `getCumulativeSectionSize`
(`lib/Fb2/Fb2.cpp:319-347`) sum chapter lengths, so progress stays monotonic and reaches
100% only if the lengths partition the body without overlap. Subtracting child spans is
exact and costs one `size_t` per open section (nesting is 2–3 deep in practice).

**Alternatives considered**: keeping the full span per chapter (parents would double-count
their children, so progress would overshoot and the percentage would be wrong); measuring
only the byte distance to the next section start (breaks for parent text after a child).

## Decision 4: delete `tocEntries`

**Decision**: remove `Fb2::tocEntries`. `getTocCount`/`getTocEntry` project `sections`
(title + level), and `getTocIndexForSectionIndex` / `getSectionIndexForTocIndex` become
the identity (kept as thin accessors so `Fb2ReaderChapterSelectionActivity` and
`Fb2ReaderActivity:459` need no churn).

**Rationale**: with one chapter per section the two vectors hold the same data, and today
every chapter title is stored **twice** in RAM (`Fb2MetadataParser.cpp:193-197`). For flat
books this change therefore *reduces* FB2 metadata RAM; for the reference book it halves
the cost of the 62 new titles. It also removes the possibility of the two lists
disagreeing.

**Alternatives considered**: adding `level` to both structs and keeping them in sync —
more memory and more invariants for zero benefit.

## Decision 5: legacy reading positions migrate through a `progress.bin` marker

**Decision**: `progress.bin` grows from 6 to 8 bytes: `u16 chapter, u16 page, u16
pageCount, u16 marker (0xFB02)`. A payload of 4 or 6 bytes has no marker, is therefore
pre-change, and its first field is interpreted as a **top-level ordinal**: the reader
resumes at the first chapter with `level == 0` counted that many times, page 0. The next
save writes the 8-byte form, so the migration is one-shot and idempotent.

**Rationale**: FR-012 requires resuming in the same part of the book. The payload length
is the only signal that survives — a flag on the `Fb2` object does not, because the
library/home screens load FB2 metadata (and would rebuild `book.bin` to v3) long before
the reader ever reads `progress.bin`. The ordinal reading is also correct for flat books,
where the ordinal equals the index, so there is no "is this book nested?" branch. Page is
reset to 0 because the saved page number indexed a chapter that no longer exists at that
size.

**Alternatives considered**: deleting `progress.bin` on cache rebuild (sends the reader
back to page 1 of the book — fails FR-012); versioning `book.bin` only (the signal is
consumed by whichever screen loads metadata first); adding a separate migration marker
file (a new file and a new failure mode for six bytes of state).

**Facts relied on**: `fb2_reader::decodeProgress` accepts sizes 4 and 6 only
(`src/activities/reader/Fb2ReaderMath.cpp:60-71`), and `Fb2ReaderActivity::loadProgress`
reads at most 6 bytes (`Fb2ReaderActivity.cpp:50-68`) — both must change together.

## Decision 6: an empty chapter still gets one page

**Decision**: if a chapter produces no completed page, `Fb2SectionParser` emits a single
empty page.

**Rationale**: this failure mode is *created* by the change. Under the old model a
top-level chapter always contained its descendants' text, so `pageCount == 0` was
unreachable; a pure wrapper section (`<section><section>…</section></section>`) now
yields a chapter with no direct content. `Fb2Section::loadPage` returns `nullptr` for
every page when `pageCount == 0` (`Fb2Section.cpp:218-221`), and
`Fb2ReaderActivity::bookProgressPercent` divides by `section->pageCount`
(`Fb2ReaderActivity.cpp:84-89`) — a zero-page chapter is a blank screen and a division by
zero. One guaranteed page is the smallest correct answer.

**Alternatives considered**: skipping empty sections when building the chapter list (the
metadata parser would need content detection it does not have today, and a titled part
divider would vanish from the list); clamping the division at the call site (treats the
symptom in one of several consumers).

## Decision 7: cache versions must both move

**Decision**: `FB2_CACHE_VERSION` 2 → 3 (`lib/Fb2/Fb2.cpp:13`) and
`FB2_SECTION_FILE_VERSION` 4 → 5 (`lib/Fb2/Fb2/Fb2Section.cpp:20`).

**Rationale**: `book.bin` gains a per-chapter `level` field, so its layout changes.
`sections/<n>.bin` keeps its layout but its *key* changes meaning — index 1 was "Part
Two", it is now "Inner Chapter" — and a stale page cache would render the wrong chapter.
Both files are rejected-and-rebuilt on mismatch by existing code paths
(`Fb2.cpp:80-83`, `Fb2Section.cpp:80`), which is exactly FR-011.

**Alternatives considered**: bumping only `book.bin` (leaves stale `sections/*.bin` keyed
under the old numbering — wrong text, no error).

## Decision 8: hierarchy is shown by indentation, no new UI

**Decision**: `Fb2ReaderChapterSelectionActivity::buildRowItems` prefixes each label with
`level * 2` spaces, mirroring the EPUB chapter list
(`EpubReaderChapterSelectionActivity.cpp:65`, which indents `(level - 1) * 2`; EPUB TOC
levels are 1-based, FB2 depths are 0-based).

**Rationale**: the convention already exists and is orientation- and theme-agnostic
because it is part of the label string. No new list component, no theme change, no new
translation.

**Alternatives considered**: a dedicated indent field in the list component (new UI
surface, must be threaded through `ListProps` and every renderer path — cost without a
user-visible difference); a depth glyph prefix (a new user-facing string and a CJK/RTL
width question for no added information).

## Decision 9: bound the chapter count

**Decision**: `FB2_MAX_CHAPTERS = 1024`. Once the counter reaches the cap, further
sections are not chapter boundaries — they render as part of the chapter that contains
them, in both parsers, by the same comparison.

**Rationale**: chapter metadata is RAM-resident. Measured cost per chapter is the
`SectionInfo` struct plus one heap title string; 66 chapters is ≈5 KB, 1024 is the point
where the worst case stays a small fraction of the 380 KB budget. Deferring rather than
dropping keeps FR-003 (no text lost) true for pathological files, and the cap also bounds
the hostile-nesting case in FR-014.

**Alternatives considered**: no cap (unbounded `std::vector` from untrusted input —
straight violation of Principle VI); dropping sections beyond the cap (loses text);
degrading back to top-level-only numbering past the cap (keeps two chapter models alive
in one binary).

## Verification approach

- Host: `bin/run-tests` plain and `--asan`. Four existing pin tests are rewritten and
  MUST be demonstrated failing against unmodified production code (Principle V).
- Device/simulator: the chapter list and reader screens for a nested book, per the
  quality gates for changes touching activities.
- Reference book: the counts and sizes in the spec's success criteria are re-measured
  from the file, not assumed.
