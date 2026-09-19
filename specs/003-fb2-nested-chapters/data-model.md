# Phase 1 Data Model: FB2 Nested Chapter Navigation

Entities as they exist after the change, with the invariants tests must hold. Field
names match the in-tree types so the plan maps 1:1 onto `lib/Fb2/Fb2.h`.

## Chapter — `Fb2::SectionInfo`

One `<section>` of a reading body. Order in the vector is document-start order, and the
vector index **is** the chapter id used by `sections/<index>.bin`, `progress.bin`, the
chapter list, and the render parser's target index.

| Field | Type | Meaning | Changed |
|-------|------|---------|---------|
| `title` | `std::string` | The section's **own** `<title>` text, paragraphs joined by a single space. Empty when the section has no direct title (the UI substitutes `tr(STR_UNNAMED)`) | Semantics: was only collected for depth-1 sections |
| `fileOffset` | `size_t` | Byte offset of the section's `<section` start tag | Unchanged (still the whole-file-fallback sentinel: count 1 + offset 0, `Fb2Section.cpp:177`) |
| `length` | `size_t` | The section's **own** byte extent: full span minus the spans of its child chapters | Semantics: was the full span including children |
| `level` | `uint8_t` | Nesting depth inside the reading body; 0 for a direct child of `<body>` | **New** |

**Invariants**

1. `sections[i].level <= sections[i-1].level + 1` — depth grows one step at a time.
2. `sum(length) == ` total bytes of all reading bodies' section spans; no two chapters'
   own extents overlap.
3. `0 < sections.size() <= FB2_MAX_CHAPTERS (1024)`; the whole-file fallback still
   produces exactly one chapter when a body has no `<section>` at all.
4. `title.size() <= FB2_CACHE_MAX_STRING (4096)` on read; the parser additionally trims
   an absurd title so RAM per chapter stays bounded.
5. Sections of an auxiliary body (second-or-later `<body>` carrying `name`) never appear.
6. Every chapter yields at least one page when rendered (guard in `Fb2SectionParser`).

## TOC projection — `Fb2::TocEntry`

No longer stored. `getTocCount()`, `getTocEntry(i)`, `getTocIndexForSectionIndex(i)` and
`getSectionIndexForTocIndex(i)` project `sections` so existing call sites
(`Fb2ReaderChapterSelectionActivity`, `Fb2ReaderActivity.cpp:459`) keep compiling.

| Field | Type | Meaning |
|-------|------|---------|
| `title` | `std::string` | Copy of the chapter title (empty → `tr(STR_UNNAMED)` at display time) |
| `sectionIndex` | `int` | Identity: equals the TOC index |
| `level` | `uint8_t` | **New** — drives the list indent (`level * 2` spaces) |

**Invariant**: `getTocCount() == getSectionCount()`, and both index mappings are the
identity for every valid index; out-of-range input keeps returning the existing empty
sentinel.

## Book metadata cache — `book.bin` (version 3)

Layout and validation in [contracts/file-formats.md](contracts/file-formats.md). Data-model
relevant points: the per-chapter record gains `level`; the trailing TOC list is removed;
`FB2_CACHE_MIN_SECTION_ENTRY` grows by one byte; the chapter count is validated against
both `FB2_MAX_CHAPTERS` and the remaining physical file size before `reserve()`.

**State transitions**

```text
book.bin version 2 (or corrupt/absent)  →  reject  →  reparse source  →  write version 3
book.bin version 3                      →  load
```

## Page cache — `sections/<index>.bin` (version 5)

Layout unchanged; the **key** changes meaning, because index 1 was "Part Two" and is now
"Inner Chapter". `FB2_SECTION_FILE_VERSION` 4 → 5 forces a rebuild, per FR-011.

## Saved reading position — `progress.bin` (version 2)

| Bytes | Field | Meaning |
|-------|-------|---------|
| 0–1 | `chapter` | u16 chapter index (v2) / top-level ordinal (legacy) |
| 2–3 | `page` | u16 page within that chapter |
| 4–5 | `pageCount` | u16 page count the index was valid for |
| 6–7 | `marker` | u16 `0xFB02` — present only in v2 |

**State transitions**

```text
8 bytes, marker == 0xFB02  →  chapter index used as-is
4 or 6 bytes (no marker)   →  legacy: chapter = firstChapterOfTopLevel(value), page = 0
any other size             →  invalid, start of book (existing behaviour)
```

`firstChapterOfTopLevel(n)` returns the index of the (n+1)-th chapter with `level == 0`,
clamped to the last chapter. For a flat book it is the identity, so no format sniffing of
the book itself is needed. The first save after a legacy read writes the 8-byte form.

## Derived quantities (unchanged code, new inputs)

- `getCumulativeSectionSize(i)` / `getBookSize()` / `calculateProgress(i, f)`
  (`lib/Fb2/Fb2.cpp:319-347`) keep working because invariant 2 holds.
- `Fb2Section`'s popup threshold reads `sectionInfo.length`; smaller chapters simply cross
  the 50 KB threshold less often.
