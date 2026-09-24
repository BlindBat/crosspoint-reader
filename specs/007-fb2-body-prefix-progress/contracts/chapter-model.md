# Contract: FB2 Chapter Model — C7 and C10 amended

Amends [specs/003-fb2-nested-chapters/contracts/chapter-model.md](../../003-fb2-nested-chapters/contracts/chapter-model.md).
Rules C1–C6, C8, C9 and the parity property are unchanged and remain in force there. Only
the two rules below are restated; where this file and specs/003 disagree, this file wins.

## C7′ — Length (amended)

Chapter *N*'s `length` is the byte span from **where its weight starts** to the end of
section *N*, minus the byte spans of its child chapters.

A chapter's weight starts at its own `<section>` start tag, **except** for the first
chapter of a reading body, whose weight starts at that body's `<body>` start tag.

Consequences:

- Chapter lengths partition **all** the bytes of the reading bodies that the reader is
  shown — their sections *and* their front matter — instead of the section bytes alone.
- `calculateProgress` stays monotonic and bounded by 100%, for the same reason as before:
  the spans still tile without overlap and without gaps.
- A body's first chapter is always top-level (no section is open when it starts), so
  widening its span never perturbs a parent's `childBytes` subtraction.
- The widening is clamped: the body offset is used only when it is strictly below the
  section offset. A byte index that is unavailable or nonsensical yields today's behaviour,
  never a wrapped `size_t`.

`fileOffset` is unaffected: it is always the `<section>` start tag, in every chapter. The
persisted record's layout is unchanged; only `ownLength` / `cumulativeLength` values move
(see [data-model.md](../data-model.md)).

## C10′ — A body's own content (amended)

A `<body>` may carry a `<title>`, `<epigraph>` or image of its own ahead of its first
`<section>` (the FB2 schema allows nothing else there). That content is in no section, so
it **reads with the first chapter of its body** — unchanged — and its bytes are **counted
in that chapter's `length`**, so it carries progress weight proportional to its size.

The previous rule ("carries no progress weight — a documented ceiling") is withdrawn. It
rested on one reference book where the front matter is a few hundred bytes of a 1,827,705-
byte body. Across the 2,899-book corpus, 380 books (13.1%) carry more than 200 bytes there,
165 (5.7%) carry at least half a displayed percentage point, and one book carries 256,971
bytes against 403,143 bytes of sections — 38.9% of the book read at a frozen percentage.

Still out of scope, and unchanged:

- **Auxiliary bodies** (C1): a second or later `<body>` with a `name` attribute contributes
  no weight, front matter included.
- **Per-body attribution**: in a book with several reading bodies, each body's front matter
  weighs into *that* body's first chapter, not into chapter 0 of the book.
- **The whole-file fallback** (C9): a reading body with no `<section>` already has one
  chapter spanning the file. Nothing is added on top of it.
- **Body-level text after the first `<section>`**: at most 1,779 bytes across 90 corpus
  books, and not rendered at all today. That is a content-loss defect, tracked separately;
  this contract neither renders nor weights it.

## Required tests

Beyond specs/003's parity property, which still applies unchanged:

1. **Weight pin** — on `test/fb2/body-prefix.fb2` (body at 188, first `<section>` at 314):
   chapter 0's `length` is 236 B (not 110) and `bookSize` is 349 B (not 223). Fails against
   a parser that has not been amended, which is this feature's Constitution V mutation
   proof.
2. **Partition** — the summed chapter lengths equal an oracle re-derived from the source
   text that counts each reading body from its `<body>` tag, for every fixture in
   `test/fb2/`.
3. **Per-body attribution** — a fixture with two unnamed bodies, each with front matter,
   weights each body's prefix into that body's own first chapter.
4. **Auxiliary body** — `notes-body.fb2` gains no weight from its named body's front matter.
5. **Rendering unchanged** — the golden page hashes for every fixture, `body-prefix.fb2`
   included, are byte-identical to the pre-change firmware's.
