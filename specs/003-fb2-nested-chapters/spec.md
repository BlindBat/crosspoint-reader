# Feature Specification: FB2 Nested Chapter Navigation

**Feature Branch**: `feature/fb2-nested-chapters`

**Created**: 2026-09-20

**Status**: Draft

**Input**: User description: "Could you find a way to improve .fb2 support? For example, there is an .fb2 book and it doesn't 'see' small chapters — only big one"

## Overview

FB2 books express structure by nesting: a collection or anthology wraps each story in
a section that sits *inside* a part-level section. CrossPoint's FB2 reader only
recognises the outermost level of that nesting, so an anthology presents as a handful
of enormous chapters and the individual stories are unreachable.

The reference book on the test card, *Рэй Брэдбери — Марсианские хроники. Полное
издание* (1.9 MB), contains 66 sections in its reading body. Only 4 of them are
outermost, so the chapter list shows 4 entries, one of which — "Марсианские хроники" —
is a single 853 KB chapter holding 30 separate stories. A reader who wants "Ракетное
лето" has no way to jump to it, the status bar reports progress in 25% steps, and
opening that chapter forces the firmware to lay out 853 KB of text before it knows the
page count.

This feature makes every FB2 section a navigable chapter with its own place in the
chapter list, its own page numbering, and its own contribution to reading progress,
while showing the book's hierarchy so a story is visibly a story *within* a part.

Scope is FB2 structural navigation only. FB2 footnote bodies, images, and styling are
untouched.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Jump straight to a story inside a collection (Priority: P1)

A reader opens an FB2 anthology, opens the chapter list, and sees every story in the
book — not just the parts that group them. They pick one story and land on its first
page.

**Why this priority**: This is the reported defect and the whole point of the feature.
Without it, most multi-story FB2 files (the dominant FB2 use case: Russian-language
collections and anthologies) are navigable only by paging through hundreds of pages.

**Independent Test**: Open the reference anthology, open the chapter list, confirm the
individual story titles are listed, select one, and confirm the reader opens at that
story's first page.

**Acceptance Scenarios**:

1. **Given** an FB2 book whose reading body nests story sections inside part sections,
   **When** the reader opens the chapter list, **Then** every section in the reading
   body appears as its own entry, at every nesting depth.
2. **Given** the chapter list shows a nested story title, **When** the reader selects
   it, **Then** the reader opens at the first page of that story and the title shown in
   the status bar / chapter indicator is that story's title.
3. **Given** the reference book *Марсианские хроники. Полное издание*, **When** the
   reader opens the chapter list, **Then** it lists 66 entries (today: 4), including
   the 62 nested story titles.
4. **Given** a flat FB2 book whose sections are all at the outermost level, **When** the
   reader opens the chapter list, **Then** the list is unchanged from today's behaviour.

---

### User Story 2 - See which part a story belongs to (Priority: P2)

The chapter list conveys the book's structure: part titles read as headings and the
stories under them read as their children, so a list of 66 entries stays legible.

**Why this priority**: Depends on Story 1 and adds no reachability, but a flat
alphabet-soup list of 66 similar titles is hard to navigate and loses the information
the FB2 file carries. The EPUB chapter list already presents nesting this way, so FB2
gaining it removes an inconsistency rather than inventing a convention.

**Independent Test**: Open the chapter list for the reference book and confirm the
four part-level entries are visually distinguishable from the story entries nested
under each of them, in document order.

**Acceptance Scenarios**:

1. **Given** an FB2 book with nested sections, **When** the reader views the chapter
   list, **Then** each entry's nesting depth is visually indicated and entries appear
   in document order.
2. **Given** a section that has no title of its own, **When** it appears in the chapter
   list, **Then** it is labelled with the localized "Unnamed" placeholder rather than
   an empty row.
3. **Given** the reader is currently inside a nested story, **When** they open the
   chapter list, **Then** the list opens with that story's entry selected.

---

### User Story 3 - Progress and page counts that reflect the story being read (Priority: P3)

Reading position, page numbers, and the progress percentage refer to the chapter the
reader is actually in, and moving from the last page of one story continues into the
next story.

**Why this priority**: Story 1 makes stories addressable; this makes the numbers around
them meaningful. It is separable — navigation is already useful with coarse
percentages — but without it a 6-page story reports "page 41 of 500".

**Independent Test**: Open a nested story in the reference book, confirm the page
counter covers only that story, page past its last page, and confirm reading continues
at the first page of the next story with progress increasing monotonically.

**Acceptance Scenarios**:

1. **Given** the reader is in a nested story, **When** they look at the page indicator,
   **Then** the page count is that story's own page count.
2. **Given** the reader is on the last page of a nested story, **When** they turn the
   page forward, **Then** the next chapter in document order opens at its first page;
   and turning back from its first page returns to the previous chapter's last page.
3. **Given** a reader progresses through the book, **When** progress is displayed at any
   point, **Then** it increases monotonically from 0% at the first page to 100% at the
   last page of the reading body.
4. **Given** a reader had a saved position in an FB2 book from before this change,
   **When** they reopen that book, **Then** they resume within the same part of the
   book they were reading (worst case its beginning), never in unrelated content and
   never out of range.

---

### Edge Cases

- **Part-level text before the first nested story** (a part title page, epigraph, or
  annotation that sits directly under a part section): must remain readable and appear
  in document order before the first story, not be dropped or duplicated.
- **Text after the last nested story inside the same parent** (rare but legal): must
  remain readable and must never be silently dropped. It reads with its parent chapter,
  ahead of that parent's children — the one accepted departure from strict document
  position (see Assumptions).
- **No sections at all** (a body of bare paragraphs): behaves as today — the whole body
  is one chapter.
- **Deeply nested sections** (three or more levels, e.g. book → part → chapter →
  scene): every level is reachable; the depth indication in the list degrades gracefully
  rather than pushing titles off the screen.
- **Many sections** — a file with hundreds of sections must not exhaust memory; the
  chapter count is bounded by a documented limit (beyond it, sections read as part of
  their containing chapter per FR-001), and a cache claiming more chapters than it can
  hold is rejected as corrupt rather than trusted.
- **Auxiliary bodies** (`name="notes"`, `name="comments"`): still excluded from chapters
  and from progress, as today — the reference book's 14 footnote sections must not
  appear in the chapter list.
- **Empty sections** (a section with a title and no content): appear in the list and
  open without error, showing at least one page.
- **Stale per-book caches** built by the previous version must be detected and rebuilt,
  not misread.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST treat every `<section>` in an FB2 reading body as its own
  chapter, at any nesting depth, and MUST list each one in the chapter list in document
  order, up to a documented chapter limit. Beyond that limit further sections MUST read as
  part of the chapter containing them rather than becoming chapters of their own, so no
  text is lost.
- **FR-002**: The system MUST record each chapter's nesting depth and MUST convey that
  depth in the chapter list, consistently with how the EPUB chapter list conveys TOC
  depth.
- **FR-003**: A chapter's text MUST be exactly its own direct content — the content of a
  parent section MUST NOT be repeated inside its children's chapters, and no text of the
  reading bodies may be lost.
- **FR-004**: The full text of the reading bodies MUST be presented across the chapter
  sequence in chapter order, and within each chapter in that chapter's own document order,
  so continuous paging from the first to the last chapter reads every part of the book
  exactly once. One deliberate exception: a parent section's own text that *follows* one of
  its child sections reads with the parent, ahead of those children (see Assumptions).
- **FR-005**: Selecting a chapter MUST open the reader at that chapter's first page.
- **FR-006**: Page counts and the in-chapter page indicator MUST refer to the selected
  chapter alone.
- **FR-007**: Reading progress MUST be computed over all reading bodies and MUST be
  monotonically non-decreasing as the reader advances, reaching 100% on the last page of
  the last chapter.
- **FR-008**: Paging forward from a chapter's last page MUST open the next chapter in
  document order, and paging back from a chapter's first page MUST return to the previous
  chapter's last page.
- **FR-009**: Chapters with no title of their own MUST display the localized "Unnamed"
  label; no user-facing string introduced by this feature may be hardcoded.
- **FR-010**: Auxiliary bodies (a second or later `<body>` carrying a `name` attribute)
  MUST remain excluded from the chapter list, from chapter numbering, and from progress.
- **FR-011**: Per-book FB2 caches written by an earlier firmware version MUST be
  detected as stale and rebuilt; the system MUST NOT interpret an old cache under the new
  chapter numbering.
- **FR-012**: A reading position saved before this change MUST resolve to a position
  within the same part of the book (its beginning at worst); it MUST NOT resolve to
  unrelated content, to an out-of-range chapter, or to a failure to open the book.
- **FR-013**: Chapter counts, per-chapter offsets, and any length or count read from a
  cache file MUST be validated against the physical file size and a documented cap
  before driving an allocation; values that fail validation MUST cause a rebuild, never
  a crash or an unbounded allocation.
- **FR-014**: Malformed nesting (unbalanced or absurdly deep `<section>` elements) MUST
  produce a bounded, deterministic outcome — the book either opens with whatever
  structure could be recovered or is reported as unreadable.
- **FR-015**: Opening a book, opening the chapter list, and jumping to a chapter MUST
  each remain within the device's memory budget for FB2 files up to at least the
  reference book's size, with no regression in peak memory relative to today's
  behaviour.

### Key Entities *(include if data involved)*

- **Chapter (FB2 reading unit)**: one `<section>` of a reading body, identified by its
  position in document order. Carries its own title (possibly empty), its nesting depth,
  the extent of its own direct text, and its own page count. Chapters partition the
  reading body without overlap.
- **Chapter list entry**: what the reader sees for one chapter — its display title (or
  the "Unnamed" placeholder) and its depth.
- **Book metadata cache**: the persisted description of a book's chapters, versioned so
  a cache produced by a different chapter model is rejected and rebuilt.
- **Saved reading position**: the chapter and page a reader left off at, which must stay
  meaningful across the chapter-model change.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For the reference book *Марсианские хроники. Полное издание*, the chapter
  list shows 66 entries instead of 4, and all 62 previously unreachable stories can be
  opened directly from it.
- **SC-002**: A reader can reach any named story in the reference book in at most two
  interactions from the reader screen (open chapter list, select story) instead of
  paging through the containing 853 KB chapter.
- **SC-003**: No chapter in the reference book exceeds the size of the largest single
  story: the largest chapter shrinks from 853.7 KB to 117.5 KB of source text - a 7.3x
  reduction in the layout work needed before a story's first page can be shown.
- **SC-004**: Reading the reference book from first page to last page reaches every
  story exactly once, in the book's printed order (no section in it carries parent text
  after a child, so FR-004's exception does not arise), with no repeated and no missing
  text.
- **SC-005**: Displayed progress advances across the reference book's chapter boundaries
  in steps of at most 6.5% of the book (its largest chapter is 117,515 bytes of a
  1,827,705-byte reading body), instead of today's four chapters of roughly 0%, 47%, 83%
  and 100%.
- **SC-006**: Flat FB2 books (no nested sections) show an identical chapter list and
  identical page counts to the current firmware.
- **SC-007**: The malformed-FB2 corpus (truncated files, unbalanced sections, hostile
  nesting depth, lying cache lengths) opens or fails deterministically with no crash and
  no sanitizer finding.

## Assumptions

- **Every section is a chapter, including parent sections.** A part-level section
  becomes a chapter holding its own front matter (its title page, epigraph, annotation)
  and ends where its first child begins. This was chosen over the alternative of making
  only leaf sections chapters, because a part's own text must land somewhere and giving
  it its own entry keeps document order, progress arithmetic, and the chapter list all
  describable by one rule. It is also why the reference book yields 66 chapters (all
  sections) rather than 63 (leaves only).
- **Depth is shown the way EPUB already shows it.** The EPUB chapter list indents nested
  TOC entries by level (`src/activities/reader/EpubReaderChapterSelectionActivity.cpp:65`);
  FB2 follows that existing convention rather than introducing new list chrome, so no new
  UI component is assumed.
- **Parent text after a child section reads with the parent.** Keeping "a chapter is a
  section's own direct content" as one rule is what guarantees no text is lost and keeps
  the progress arithmetic exact; the price is that this rare shape reads slightly out of
  printed position. Splitting the parent into pre-child and post-child chapters would add
  an untitled row to the list for every occurrence.
- **A documented chapter limit of 1024.** Chapter titles and extents live in RAM, so the
  count must be bounded on a 380 KB device. Past the limit, sections stop being chapter
  boundaries and read as part of their container — bounded memory without losing text. The
  limit is a constant, raisable once chapter metadata moves to the SD card.
- **Terminology**: this spec says "chapter"; the firmware and its cache paths say
  "section" (the FB2 markup name). They are the same thing.
- **All depths are listed; no depth cap.** FB2 files in practice nest two or three levels
  deep. Collapsing or hiding deep levels would re-create the reported defect at a
  different depth.
- **Progress stays size-proportional.** Progress continues to be estimated from each
  chapter's share of the reading body's bytes, as today; finer chapters make the estimate
  better without needing a new progress model.
- **A one-time cache rebuild is acceptable.** Existing FB2 per-book caches are rebuilt on
  first open after the update; users see the normal "building" behaviour once per book.
- **Out of scope**: FB2 footnote bodies and footnote links, FB2 images, FB2 styling
  beyond what is rendered today, EPUB/TXT/XTC formats, and any change to the reading
  page's layout or typography.
- **Verification environment**: correctness is proven by host tests over committed FB2
  fixtures (including the nesting shapes above); the chapter list and reader screens are
  verified in the simulator, per the project's quality gates.
