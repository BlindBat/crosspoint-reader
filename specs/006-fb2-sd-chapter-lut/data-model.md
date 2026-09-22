# Data Model: FB2 Chapter Metadata on SD

The on-disk layout is in [contracts/book-bin-v5.md](contracts/book-bin-v5.md). This file covers
what lives in memory and when.

## `Fb2` (one per open book): what it holds

| Field | Type | Notes |
|---|---|---|
| `filepath`, `cachePath`, `title`, `author`, `language`, `coverBinaryId` | `std::string` | unchanged |
| `chapterCount` | `uint16_t` | 1..65,535 |
| `recordsOffset` | `uint32_t` | byte offset of record 0 in `book.bin` |
| `titlesOffset` | `uint32_t` | byte offset of the title area |
| `bookSize` | `uint32_t` | `cumulativeLength` of the last record |
| `loaded` | `bool` | unchanged |

**Removed**: `std::vector<SectionInfo> sections`. Nothing in `Fb2` scales with chapter count.

## `Fb2::SectionInfo`: a value, never stored per chapter

The same struct, returned **by value** from a lookup. One gains a field:

| Field | Type | Source |
|---|---|---|
| `title` | `std::string` | title area, `titleLength` bytes at `titleOffset` |
| `fileOffset` | `size_t` | record |
| `length` | `size_t` | record `ownLength` |
| `cumulativeLength` | `size_t` | **new**: running total of `length` through this chapter |
| `level` | `uint8_t` | record |
| `titleDerived` | `uint8_t` | record `flags` bit 0 |

Copies of it exist only where the code keeps one:
- the reader's `chapterInfo`, a single copy for the current chapter;
- the chapter list's window: 24 labels, built from these values and then dropped;
- temporaries at each call site;
- during a parse, one per **open** section in the parser's stack (at most 7 in the corpus).

## Parse-time state (`Fb2MetadataParser`)

`OpenSection` gains `Fb2::SectionInfo info` and the chapter index. It no longer holds an
`entryIndex` into a vector. Titles and labels are written into `openSections.back().info`.
Every current write site already targets `back()` (`Fb2MetadataParser.cpp:154,166,240,253`).

The chapter counter replaces `sections.size()`. Two transitions reach the sink:
- **start tag**: counter below 65,535 → `sink.reserve()`, push an `OpenSection`, and increment the counter.
- **end tag**, or `</body>` closing still-open sections (`:285`): `sink.write(index, info)` with
  the final length. The same 0 length as today applies when a section is never closed.

With no `<section>` at all, the parser passes one whole-file chapter through the same sink,
titled with the book title.

## Reader state (`Fb2ReaderActivity`)

| Field | Purpose |
|---|---|
| `Fb2::SectionInfo chapterInfo` | status-bar title plus progress inputs for the current chapter |
| `int chapterInfoIndex = -1` | index `chapterInfo` was read for; a mismatch at the top of `renderBook()` triggers a re-read |

## Validation rules (enforced at load; any failure rejects the file and the book is re-parsed)

1. `version == 5`; each header string ≤ 4,096 bytes.
2. `1 ≤ chapterCount`, and `recordsOffset + chapterCount·20 + titlesSize == fileSize` exactly.
3. For each record *i*, in order:
   - `titleLength ≤ 4,096` and `titleOffset + titleLength ≤ titlesSize`;
   - `flags & ~0x01 == 0`, and a derived flag never sits on an empty title;
   - `level ≤ previous level + 1`, and `level == 0` for *i* = 0;
   - `cumulativeLength == previous cumulative + ownLength`, checked with overflow detection.

## State transitions: `book.bin` lifecycle

```text
absent / wrong version / corrupt ──load()──▶ parse ─▶ chapters.tmp + titles.tmp
                                                  └▶ assemble ─▶ book.bin (v5) ─▶ remove tmps
rejected version < 5 && new chapterCount > 256 ──▶ also removeDir(sections/)
parse or assembly fails ──▶ remove book.bin, chapters.tmp, titles.tmp; load() returns false
```
