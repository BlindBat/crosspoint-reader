# Contract: FB2 Cache File Formats (v4)

One on-SD format changes. Little-endian; `string` means `u32 length` followed by that many
bytes, no terminator, as `serialization::writeString` writes. `docs/file-formats.md` MUST be
updated in the same commit (AGENTS.md cache-versioning rule).

## `book.bin` — version 3 → **4**

Constant: `FB2_CACHE_VERSION` ([lib/Fb2/Fb2.cpp:16](../../../lib/Fb2/Fb2.cpp)).

```text
u8      version                  # 4
string  title
string  author
string  language
string  coverBinaryId
u16     chapterCount             # 1 .. FB2_MAX_CHAPTERS  (now 256, was 1024 — see research.md M1-M3)
repeat chapterCount times:
  string  title                  # own title, OR a derived first-line label, may be empty
  u32     fileOffset             # offset of its <section start tag
  u32     ownLength              # span minus child chapters' spans
  u8      level                  # 0 = direct child of <body>
  u8      flags                  # bit0 = title was derived; all other bits reserved 0   <-- NEW
```

**Reader validation (all before any `reserve()` or `resize()`)**

| Check | On failure |
|-------|-----------|
| `version == 4` | reject → reparse source |
| `1 <= chapterCount <= FB2_MAX_CHAPTERS` (256) | reject |
| `chapterCount * FB2_CACHE_MIN_SECTION_ENTRY <= bytes remaining in file` | reject |
| every `string length <= FB2_CACHE_MAX_STRING` (4096) | reject |
| every read is a full read (`readPodChecked` / `readStringChecked`) | reject |
| `level <= previous level + 1`, first chapter `level == 0` | reject |
| `flags & ~0x01 == 0` | reject |
| `flags & 0x01` implies the entry's `title` is non-empty | reject |

`FB2_CACHE_MIN_SECTION_ENTRY` becomes `4 + 4 + 4 + 1 + 1` (empty-string prefix, two u32s, the
level byte, the flags byte).

Rejection is never fatal: `Fb2::load()` reparses the source file and rewrites the cache
(existing path, [lib/Fb2/Fb2.cpp:186-206](../../../lib/Fb2/Fb2.cpp)).

**Compatibility**

- A v3 cache is rejected by the version check and rebuilt: **one expat metadata pass per book**,
  on first open after the update.
- A cache written before the ceiling dropped, holding more than 256 chapters, is rejected by the
  existing `chapterCount > FB2_MAX_CHAPTERS` check and rebuilt at the new ceiling. No new code.
- `sections/<index>.bin` and `progress.bin` are **unchanged**. No book is re-paginated and no
  reading position moves.

## Strings written

A **derived** label written to `book.bin` is at most `FB2_MAX_LABEL_CHARS` (64) **characters**,
truncated on a UTF-8 character boundary by `utf8TruncateChars` — a `<p>` is prose and would
otherwise be unbounded. A real `<title>` is written as the book supplies it, bounded on read by
the existing `FB2_CACHE_MAX_STRING` (4096); measured across 2,899 real books, capping real
titles too would save 3% of the worst book's metadata, which does not pay for changing what the
reader sees (research.md Decision 4).

Measured per-chapter cost in RAM (riscv32, from the object file): 36 B of `SectionInfo` plus one
heap block of `align4(len+1) + 8` for any title longer than 15 characters. At the 256 ceiling
the worst real book in the corpus costs **43,732 B**.
