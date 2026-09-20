# Contract: FB2 Chapter Model

Normative rules that **both** FB2 parsers must implement identically —
`Fb2MetadataParser` (which numbers and describes chapters) and `Fb2SectionParser` (which
renders chapter *N*). If the two ever disagree, the reader opens the wrong text, so these
rules are the contract, and the numbering-parity test is the one test that must never be
deleted.

## C1 — Reading bodies

A `<body>` is a **reading body** unless it is the second or later `<body>` *and* carries a
`name` attribute. Sections of a non-reading body are not chapters and contribute no
bytes to progress. (Unchanged behaviour; both parsers already agree here —
`Fb2MetadataParser.cpp:83-90`, `Fb2SectionParser.cpp:108-117`.)

## C2 — Numbering

Chapters are numbered from 0, in the order their `<section>` **start tags** appear inside
reading bodies, at every depth. Numbering is not affected by:

- whether the section has a title, content, or children;
- whether an enclosing chapter's content is currently being rendered or suppressed;
- nesting depth.

Numbering **is** stopped by C6 (the cap).

## C3 — Extent

Chapter *N*'s text is the **direct content** of section *N*: everything inside it except
the subtrees of its child `<section>` elements, which are chapters of their own. Direct
content that appears after a child section still belongs to chapter *N*, and therefore
reads before its children.

Consequences (all testable on `test/fb2/nested-sections.fb2`):

| Chapter | Level | Title | Contains | Must NOT contain |
|---------|-------|-------|----------|------------------|
| 0 | 0 | `Part One The Beginning` | `Outer opening paragraph.`, `Outer closing paragraph.` | `innerword` |
| 1 | 1 | `Inner Chapter` | `Nested innerword paragraph.` | `Outer`, `level` |
| 2 | 0 | `Part Two` | `Second top level paragraph.` | `innerword` |

## C4 — Title

A chapter's title is the text of the `<title>` element that is a **direct child** of its
own `<section>`, with its `<p>` children joined by a single space. A `<title>` belonging to
a nested `<section>`, to a `<poem>`, or to any other descendant is not the chapter's title.
A chapter with no direct title has an empty title, and the UI renders `tr(STR_UNNAMED)`.

## C5 — Level

A chapter's `level` is the number of enclosing `<section>` elements inside its reading
body: 0 for a direct child of `<body>`. Levels restart at 0 in each reading body.

## C6 — Cap

`FB2_MAX_CHAPTERS = 1024` (lowered to 256 by `specs/004-fb2-reader-hardening`; the rule below is
unchanged, only the number). Once the ceiling is reached, further `<section>`
elements are **not** chapter boundaries: they render as part of the chapter containing
them (so no text is lost) and they get no list entry. Both parsers apply the cap at the
same point in the same order, so the numbering stays identical.

## C7 — Length

Chapter *N*'s `length` is the byte span of section *N* minus the byte spans of its child
chapters. Chapter lengths therefore partition the reading bodies' section bytes, which is
what keeps `calculateProgress` monotonic and bounded by 100%.

## C8 — Minimum output

Rendering a chapter always produces at least one page. A chapter whose direct content is
empty (a pure wrapper section) produces one blank page rather than zero pages.

## C9 — Whole-file fallback

When a reading body contains no `<section>` at all, there is exactly one chapter with
`fileOffset == 0` spanning the file, and the render parser is invoked with target index
`-1` to process all body content (existing behaviour, `Fb2Section.cpp:176-177`).

## C10 — A body's own content

A `<body>` may carry a `<title>`, `<epigraph>` or image of its own ahead of its first
`<section>` (the FB2 schema allows nothing else there). That content is in no section, so
it reads with the **first chapter of its body** and is never dropped. Its bytes are in no
chapter's `length`, so it carries no progress weight — a documented ceiling, since it is a
few hundred bytes of a whole body.

## Parity property (required test)

For every fixture in `test/fb2/`, and for each chapter index `i` reported by
`Fb2MetadataParser`:

- `Fb2SectionParser` with target `i` produces text that includes the chapter's own words
  and excludes every child chapter's words;
- `Fb2SectionParser` with target `getSectionCount()` produces no text (no off-by-one at
  the end of the numbering);
- concatenating the text of chapters `0..count-1` yields every word of the reading bodies
  exactly once.

The third clause is the machine-checkable form of FR-003 and FR-004 and is the test that
catches a numbering drift between the two parsers.
