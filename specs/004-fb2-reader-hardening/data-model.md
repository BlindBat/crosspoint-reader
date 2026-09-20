# Phase 1 Data Model: FB2 Reader Hardening

Only the deltas from `003-fb2-nested-chapters` are listed. Everything not mentioned is
unchanged.

## `Fb2::SectionInfo` (in RAM, one per chapter)

| Field | Type | Change | Meaning |
|---|---|---|---|
| `title` | `std::string` | **capped** | The section's own title, **or** a derived first-line label when it had none. Bounded to `FB2_MAX_TITLE_CHARS` characters. Empty only when the section has neither. |
| `fileOffset` | `size_t` | — | Offset of its `<section` start tag. |
| `length` | `size_t` | — | Own bytes: full span minus child chapters' spans. |
| `level` | `uint8_t` | — | Nesting depth in the body; 0 = direct child of `<body>`. |
| `titleDerived` | `bool` | **new** | True when `title` was derived from the section's first paragraph rather than read from a `<title>`. Display-only. |

Invariants (validated on cache load, before any allocation the value drives):

1. `0 < chapterCount <= FB2_MAX_CHAPTERS` — **now 256**, was 1024.
2. `chapterCount * FB2_CACHE_MIN_SECTION_ENTRY <= remaining file bytes`.
3. Every stored string `<= FB2_CACHE_MAX_STRING` (4096) on read; every string *written* is
   `<= FB2_MAX_TITLE_CHARS` characters.
4. First `level == 0`; `level <= previous + 1`.
5. `flags` has no bit set other than bit 0 — any other bit is corruption.
6. `titleDerived` implies `!title.empty()`.

## Constants

| Constant | Where | Value | Role |
|---|---|---|---|
| `Fb2::FB2_MAX_CHAPTERS` | `lib/Fb2/Fb2.h` | `1024` → **`256`** | The single source of truth for the ceiling (FR-007). Mirrored in `docs/file-formats.md` and the `003` spec documents, never re-declared in code. |
| `Fb2::FB2_MAX_TITLE_CHARS` | `lib/Fb2/Fb2.h` | **new, `64`** | Character cap for a stored title or derived label (FR-018). |
| `FB2_CACHE_VERSION` | `lib/Fb2/Fb2.cpp` | `3` → **`4`** | v4 adds the per-chapter `flags` byte. |
| `FB2_CACHE_MIN_SECTION_ENTRY` | `lib/Fb2/Fb2.cpp` | `+1` | Grows by the `flags` byte, so the pre-`reserve()` size check stays honest. |
| `FB2_SECTION_FILE_VERSION` | `lib/Fb2/Fb2/Fb2Section.cpp` | **unchanged** | No page cache is invalidated. |

## Chapter list window (`Fb2ReaderChapterSelectionActivity`)

Replaces the two `std::vector`s.

| Member | Type | Meaning |
|---|---|---|
| `TOC_WINDOW` | `static constexpr int = 24` | Rows materialized at once, matching EPUB. |
| `windowLabels` | `std::string[TOC_WINDOW]` | Indent prefix + label for each row in the window. |
| `windowItems` | `freeink::ui::ListItem[TOC_WINDOW]` | Handed to `screen.list()` with `itemsWindowFirst = windowStart`. |
| `windowStart` | `int`, init `-1` | Absolute index of row 0 of the window; `-1` = nothing materialized. |
| `windowCount` | `int` | Rows actually filled (the tail window is shorter). |

Invariants: `0 <= windowStart <= max(0, total - TOC_WINDOW)`; `windowCount == min(TOC_WINDOW,
total - windowStart)`; a row's `actionValue` is its **absolute** chapter index, so selection
semantics are unchanged.

## Reader cover state (`Fb2ReaderActivity`)

| Member | Type | Meaning |
|---|---|---|
| `onCoverPage` | `bool` | The cover is what the next render draws. Never persisted. |
| `coverBmpPath` | `std::string` | Resolved once at book load; empty when the book has no usable cover. |

State transitions:

| From | Event | To |
|---|---|---|
| book load, restored position is chapter 0 / page 0, cover exists | — | `onCoverPage = true` |
| book load, any other restored position, or no cover | — | `onCoverPage = false` |
| `onCoverPage` | page forward | `false` (chapter 0, page 0 renders) |
| `onCoverPage` | page back | `false` — nothing before the cover |
| chapter 0, page 0, cover exists | page back | `true` |
| `onCoverPage` | chapter selected from the menu, percent jump, skip | `false` |

`onCoverPage` never affects `currentSectionIndex`, `section->currentPage`,
`bookProgressPercent()`, or what `saveProgress` writes.

## Persisted formats

- `book.bin` — v4, see [contracts/file-formats.md](contracts/file-formats.md).
- `progress.bin` — **unchanged**, same fields and same meaning. A position saved before this
  feature restores to the same chapter and page after it.
- `sections/<n>.bin` — **unchanged**, not invalidated.
- `cover.bmp` — **read only**; produced by the existing extractor, never written by this feature.
