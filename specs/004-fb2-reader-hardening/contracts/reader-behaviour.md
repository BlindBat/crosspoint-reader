# Contract: Chapter Labels and the Cover Page

Rule ids are what the tests and tasks cite. Rules L1–L8 are host-testable through
`Fb2::load()`; rules V1–V7 are reader behaviour, testable on the simulator or device and by
inspection of the state machine in [data-model.md](../data-model.md).

## Chapter labels (L)

- **L1** — A `<section>` with a `<title>` of its own keeps that title, unchanged from
  `003-fb2-nested-chapters` contract rules C4/C5. Its `titleDerived` flag is false.
- **L2** — A `<section>` with no `<title>` of its own takes its label from the text of its own
  first `<p>`, and its `titleDerived` flag is true.
- **L3** — "Its own" excludes descendants: text inside a child `<section>` never labels the
  parent, exactly as a child's `<title>` never titles the parent (C4).
- **L4** — Only the first `<p>` contributes. Text after that paragraph is not appended, even if
  the label is shorter than the cap.
- **L5** — Leading and trailing whitespace is stripped, and internal runs of whitespace are
  collapsed to single spaces, before the cap is applied.
- **L6** — A **derived** label is truncated to `FB2_MAX_LABEL_CHARS` (64) characters on a UTF-8
  character boundary. A real title (L1) is **not** truncated at parse time — only at display.
- **L7** — A section with no title and no printable text of its own keeps an empty stored title;
  the chapter list substitutes the localized `STR_UNNAMED` placeholder at display time, as it
  does today.
- **L8** — Labels are display data only. Deriving one changes no chapter index, no chapter
  ordering, no `fileOffset`, no `ownLength`, and no progress value.

**Display**: a row whose `titleDerived` is true is rendered through a translated format string
so it reads as the book's words rather than as a title the book supplied (FR-016). The indent
prefix by `level` is applied to derived and real labels alike.

## Cover page (V)

- **V1** — A book whose cover bitmap is available and whose restored position is chapter 0 /
  page 0 opens on the cover.
- **V2** — A book with no cover, or whose cover bitmap cannot be opened or whose headers do not
  parse, opens on chapter 0 / page 0 with no blank page and no error screen.
- **V3** — A book restored to any position other than chapter 0 / page 0 opens on that position.
  The cover is not shown.
- **V4** — A forward page turn from the cover renders chapter 0 / page 0. A back page turn from
  chapter 0 / page 0, in a book that has a cover, renders the cover. A back turn from the cover
  does nothing.
- **V5** — While the cover is displayed, the current chapter is 0, the current page is 0, the
  progress percentage is the same value chapter 0 / page 0 reports, and the reader menu shows
  that chapter's real page count.
- **V6** — Displaying the cover writes no reading position that differs from chapter 0 / page 0,
  and never invalidates or rewrites `sections/<n>.bin`.
- **V7** — Leaving the cover by any route other than a forward turn — chapter selection, a
  percent jump, a chapter skip, a settings change that re-paginates — lands on the target
  position with the cover no longer displayed.

**Placement**: the cover is scaled with
`sleepimage::calculateBitmapPlacement(bitmapW, bitmapH, screenW, screenH, crop = false)`, the
same helper the sleep screen uses, so the whole cover is visible and centred at any orientation.
