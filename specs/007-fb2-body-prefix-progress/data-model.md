# Phase 1 Data Model: FB2 Body-Level Front Matter Carries Progress Weight

No entity is added. One parser field is added and one persisted field changes meaning.

## Parser state — `Fb2MetadataParser`

| Field | Type | Lifetime | Meaning |
|---|---|---|---|
| `bodyPrefixStart` (**new**) | `size_t` | set on a reading `<body>`, cleared when consumed and at `</body>` | Byte offset of the reading body's `<body>` start tag, pending attachment to that body's first chapter. `NO_BODY_PREFIX` (a sentinel) when nothing is pending. |
| `openSections.back().startOffset` (**meaning widened**) | `size_t` | while the section is open | Where this chapter's byte span *starts*. Its own `<section>` tag, except for a body's first chapter, where it is the `<body>` tag. |
| `openSections.back().info.fileOffset` (**unchanged**) | `size_t` | persisted | Always the `<section>` start tag. Never the body tag. |

**Invariant**: `startOffset <= info.fileOffset`, with equality for every chapter except a
body's first. The assignment is clamped to preserve it, so an unavailable or hostile byte
index degrades to equality rather than underflowing (FR-008).

**Transitions**:

```text
<body> (reading)            -> bodyPrefixStart = byteIndex
<body> (auxiliary, named)   -> unchanged (no weight, FR-005)
<section>, prefix pending   -> startOffset = min(bodyPrefixStart, byteIndex); prefix consumed
<section>, none pending     -> startOffset = byteIndex
</body>                     -> bodyPrefixStart cleared (FR-006: the next reading body gets its own)
```

## Chapter weight — `Fb2::SectionInfo`

| Field | Before | After |
|---|---|---|
| `length` | section span − child chapter spans | **first chapter of a body**: (section span + its body's front matter) − child chapter spans; every other chapter unchanged |
| `cumulativeLength` | running total of `length` | unchanged rule, larger values |
| `fileOffset`, `level`, `title`, `titleDerived` | — | untouched |

`bookSize` is the last chapter's `cumulativeLength`, so it grows by exactly the reading
bodies' front matter and the partition stays exact (FR-002).

## Persisted format — `book.bin`

Record layout is byte-identical to v5
([specs/006 book-bin-v5.md](../006-fb2-sd-chapter-lut/contracts/book-bin-v5.md)): 20 bytes,
same field order, same offsets. Only the *values* of `ownLength` / `cumulativeLength`
change, which is invisible to a reader and therefore exactly what the version byte is for.

| | v5 | v6 |
|---|---|---|
| Version byte | 5 | 6 |
| Record layout | 20 B, unchanged | 20 B, unchanged |
| `ownLength` of a body's first chapter | excludes body front matter | includes it |
| Companion data on rejection | `sections/` dropped when the old cache predates the current version and the book has > 256 chapters | that clause pinned to pre-v5 caches, so a v5 → v6 rejection keeps `sections/` |

`progress.bin` is not versioned by this change and is not rewritten: it stores
`sectionIndex` + `page` + `pageCount`, and chapter numbering is untouched (FR-010).
