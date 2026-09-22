# Feature Specification: FB2 Body-Level Front Matter Carries Progress Weight

**Feature Branch**: `feature/fb2-body-prefix-progress`

**Created**: 2026-09-22

**Status**: Draft

**Input**: GitHub issue [BlindBat/crosspoint-reader#9](https://github.com/BlindBat/crosspoint-reader/issues/9) — "FB2 body-level front matter carries no progress weight". User description: "with /ponytail full issue #9".

## Summary

An FB2 `<body>` may carry text of its own — a part title, an epigraph, or, in the
worst real case, an entire novel's worth of paragraphs — ahead of its first
`<section>`. That text is shown to the reader, at the front of the body's first
chapter, but it counts for nothing in the book's progress percentage: the
percentage is computed over section bytes only. While the reader is inside that
front matter the percentage does not move, and once they leave it the percentage
jumps.

The issue was filed as "probably never worth fixing" on the strength of one
reference book, where the front matter is a few hundred bytes of a 1,827,705-byte
body. A scan of the full 2,899-book FB2 corpus contradicts that: **380 books
(13.1%) carry more than 200 bytes of body-level front matter, 165 (5.7%) carry
enough to move the displayed percentage by at least half a point, and one book
carries 256,971 bytes of it against 403,143 bytes of section content — 38.9% of
the book, read at a frozen percentage.** This feature makes that text count.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The percentage moves while reading front matter (Priority: P1)

A reader opens an FB2 book whose text begins before its first `<section>` — the
extreme case being a book where nearly two fifths of the prose sits there. They
read forward page by page. The percentage climbs steadily, at the same rate per
page as it does in the rest of the book.

**Why this priority**: This is the defect. In the worst corpus book the reader
spends 39% of the book watching a percentage that does not move, then sees it jump.
Every other requirement here exists to make this change safe.

**Independent Test**: Open the worst-case corpus shape (a body whose direct
paragraphs outweigh its sections) with no cache present, note the percentage on the
first page, read to the last page before the first `<section>`, and compare. The
percentage must have advanced roughly in proportion to the bytes read, not stayed
at its starting value.

**Acceptance Scenarios**:

1. **Given** a book whose body carries text ahead of its first `<section>`, **When** the reader turns from the first page of the book to the last page of that front matter, **Then** the displayed percentage increases.
2. **Given** the reader is on the last page of a body's front matter, **When** they turn into the first `<section>`'s own text, **Then** the percentage continues from where it was — the front matter and the section are one chapter's span, so FR-002's exact partition leaves no discontinuity to jump across.
3. **Given** a book whose front matter is only the `<body>` start tag and whitespace — the ordinary case — **When** the reader reads it, **Then** the displayed percentage is unchanged from today's behaviour at every page.
4. **Given** any FB2 book, **When** the reader reaches the final page of the final chapter, **Then** the percentage reads 100%, and on the first page of the first chapter it reads 0%.

---

### User Story 2 - No book loses or gains text, and none re-reads wrong (Priority: P1)

Nothing the reader sees on a page changes. Front matter still appears at the front
of the body's first chapter; chapter titles, the chapter list, chapter boundaries,
and the page a saved position restores to are all exactly as before.

**Why this priority**: Equal-first. Progress weight is a number attached to
chapters; getting it right is worthless if it perturbs what a chapter *contains* or
where the reader's saved position lands. A book that reopens on the wrong page is a
far worse defect than one whose percentage stalls.

**Independent Test**: For every FB2 fixture in the test corpus, compare rendered
page content and chapter boundaries before and after the change — they must be
byte-identical — while the chapter weights differ only for bodies that carry front
matter.

**Acceptance Scenarios**:

1. **Given** any FB2 book, **When** its chapters are rendered, **Then** every page's content and every chapter boundary is identical to the previous firmware's.
2. **Given** a book with a saved reading position, **When** it is reopened after the firmware update, **Then** the reader lands on the same page of the same chapter as before the update.
3. **Given** a book whose second `<body>` is auxiliary (carries a `name`, e.g. footnotes), **When** progress is computed, **Then** that body's content — its front matter included — still carries no weight, exactly as today.
4. **Given** a book with more than one reading body, **When** progress is computed, **Then** each reading body's front matter is weighted into that body's own first chapter, not into chapter 0 of the book.

---

### User Story 3 - Books cached by the old firmware recover silently (Priority: P2)

A reader who already has FB2 books open on the device updates the firmware. Their
books still open, still show a correct percentage, and they are not asked to do
anything.

**Why this priority**: Chapter weights are persisted on the SD card. A stale cache
that is silently trusted would show a percentage computed on the old rule against a
book indexed under the new one — a wrong number with no symptom to report. It is P2
only because the recovery path (reject and rebuild) already exists for every other
format-version change.

**Independent Test**: Build a book's cache with the pre-change firmware, update, and
open the book. The cache is rebuilt without a user-visible error, and the resulting
percentages match a clean build from scratch.

**Acceptance Scenarios**:

1. **Given** a cache written before this change, **When** the book is opened afterwards, **Then** the cache is rebuilt rather than trusted, and no error is shown to the reader.
2. **Given** a cache written after this change, **When** the book is reopened, **Then** the cache is reused with no rebuild.

---

### Edge Cases

- **A body with no `<section>` at all** (10 corpus books): the whole file is already treated as one chapter, so its weight is already complete. This must stay unchanged rather than double-counting the front matter.
- **A body whose front matter is its entire text** — sections carry only a trailing afterword: the front matter dominates the book's weight, and that is the correct outcome.
- **Body-level text *after* the first `<section>`** (90 books, at most 1,779 bytes): out of scope. That text is not rendered at all today, which is a content defect rather than a progress defect; this feature must not make it worse, and fixing it belongs to its own issue.
- **Unbalanced markup** — a body that never closes, or sections left open at `</body>`: chapters already emitted with zero length must keep degrading the same way, with no negative or wrapped weight.
- **A truncated or hostile file** where the first `<section>` appears before the `<body>` start offset, or byte offsets run backwards: the weight contributed must clamp to zero rather than underflow.
- **The chapter-count ceiling**: in a book with more sections than the chapter index holds, the front matter still belongs to the body's first indexed chapter.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A reading body's bytes ahead of its first `<section>` MUST be counted in the progress weight of the chapter that displays them — that body's first chapter.
- **FR-002**: Chapter weights MUST continue to partition the counted text exactly: every counted byte belongs to exactly one chapter, and the sum of all chapter weights MUST equal the book's total weight.
- **FR-003**: Which text appears on which page, and where chapters begin and end, MUST NOT change for any book.
- **FR-004**: The progress percentage MUST read 0% at the first page of the first chapter and 100% at the last page of the last chapter, for every book.
- **FR-005**: Auxiliary bodies (a second or later `<body>` carrying a `name` attribute) MUST remain entirely unweighted, front matter included.
- **FR-006**: In a book with several reading bodies, each body's front matter MUST be weighted into that body's own first chapter.
- **FR-007**: A book whose reading body contains no `<section>` MUST keep its current whole-file weight, with no front matter added on top.
- **FR-008**: Byte arithmetic MUST be clamped so malformed input contributes zero rather than an underflowed or negative weight; a malformed file MUST fail deterministically and sanitizer-clean, as every other FB2 path does.
- **FR-009**: Caches written by firmware without this change MUST be rejected and rebuilt rather than read, and the rebuild MUST be invisible to the reader beyond the usual indexing indication.
- **FR-010**: A reader's saved position MUST restore to the same page of the same chapter across the update.
- **FR-011**: The behaviour MUST be pinned by a host test that fails against the unfixed parser, covering at minimum: front matter weighted into the first chapter, an auxiliary body still unweighted, the multi-body case, and the no-section fallback.

### Key Entities

- **Reading body**: the first `<body>` of an FB2 file, plus any later `<body>` with no `name` attribute. Its text is the book's reading content.
- **Body front matter**: the bytes of a reading body from its start tag up to its first `<section>` start tag — typically the tag and whitespace alone, sometimes a part title and epigraph, occasionally the bulk of the book's prose.
- **Chapter**: one `<section>` at any depth within a reading body. Carries its own displayed text, its own byte weight, and its running total across the book.
- **Chapter weight**: the byte count attributed to a chapter, used as its share of the progress percentage. Today the section's own span minus its child chapters'; after this change, plus its body's front matter when it is the body's first chapter.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In the worst corpus book (256,971 bytes of front matter against 403,143 bytes of sections, measured over the 2,899-book FB2 corpus on 2026-09-22), the percentage advances across the front matter instead of holding at its opening value for 39% of the book. *Verified on hardware (quickstart §6), not by the host suite: the host pins prove the rule on `body-prefix.fb2`, and the corpus supplies the motivation rather than a second oracle.*
- **SC-002**: For the 165 corpus books (5.7%) whose front matter is worth at least half a displayed percentage point, the percentage shown at the end of the front matter differs from the percentage shown at its start. *Follows from SC-001's rule once proven; spot-checked on hardware, not enumerated.*
- **SC-003**: For the remaining books — those whose front matter is the `<body>` tag and whitespace — the displayed percentage on every page is unchanged from the previous firmware.
- **SC-004**: Rendered page content and chapter boundaries are identical to the previous firmware for 100% of FB2 test fixtures.
- **SC-005**: The full host test program passes plain and under ASan+UBSan, and the new pin test is demonstrated to fail against the unfixed parser.
- **SC-006**: No additional heap allocation and no additional pass over the file: the change adds only fixed-size bookkeeping to a parse that already runs. *Established by inspection of the diff — one `size_t` member, one clamped assignment, no new allocation site and no new traversal — not by a counter. An alloc-count pin would pass identically before and after, so it would assert nothing.*

## Assumptions

- **The reference measurement is the fork's 2,899-book FB2 corpus** (`~/Calibre Library`, scanned 2026-09-22 with an expat scan mirroring the metadata parser's own offset arithmetic). Every count and byte figure in this spec comes from that scan, not from estimation.
- **"Reads with the first chapter" stays the rendering rule.** This feature changes only what the front matter *weighs*, never where it is shown. Giving body front matter a chapter entry of its own was rejected in issue #9 and is still rejected: it would add a chapter-list row to the 100% of books that have a body.
- **Weight is measured in source bytes**, as it already is throughout the FB2 reader. Markup overhead is counted the same way for front matter as for sections, so the two are weighted on the same scale.
- **Persisted chapter records change shape or meaning**, so the existing cache-version mechanism is the intended invalidation route; no new migration path is introduced.
- **Body-level text after the first section is a separate defect** and is deliberately left alone here.
- **Progress synchronisation with external services** (KOReader) consumes the same percentage and therefore follows this change automatically; no separate mapping work is assumed.
