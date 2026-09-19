# Contract: FB2 Cache File Formats

Three on-SD formats change. All are little-endian; `string` means `u32 length` followed by
that many bytes (no terminator), as `serialization::writeString` already writes.
`docs/file-formats.md` MUST be updated in the same commit (AGENTS.md cache-versioning
rule).

## `book.bin` — version 2 → **3**

Constant: `FB2_CACHE_VERSION` (`lib/Fb2/Fb2.cpp:13`).

```text
u8      version                  # 3
string  title
string  author
string  language
string  coverBinaryId
u16     chapterCount             # 1 .. FB2_MAX_CHAPTERS
repeat chapterCount times:
  string  title                  # the chapter's OWN title, may be empty
  u32     fileOffset             # offset of its <section start tag
  u32     ownLength              # span minus child chapters' spans
  u8      level                  # 0 = direct child of <body>       <-- NEW
# the trailing TOC list of version 2 is REMOVED
```

**Reader validation (all before any `reserve()` or `resize()`)**

| Check | On failure |
|-------|-----------|
| `version == 3` | reject → reparse source |
| `1 <= chapterCount <= FB2_MAX_CHAPTERS` | reject |
| `chapterCount * FB2_CACHE_MIN_SECTION_ENTRY <= bytes remaining in file` | reject |
| every `string length <= FB2_CACHE_MAX_STRING` (4096) | reject |
| every read is a full read (`readPodChecked` / `readStringChecked`) | reject |
| `level <= previous level + 1`, first chapter `level == 0` | reject |

`FB2_CACHE_MIN_SECTION_ENTRY` becomes `4 + 4 + 4 + 1` (empty-string prefix, two u32s, the
level byte). `FB2_CACHE_MIN_TOC_ENTRY` and the TOC read loop are deleted.

Rejection is never fatal: `Fb2::load()` reparses the source file and rewrites the cache
(existing path, `Fb2.cpp:80-83`).

## `sections/<index>.bin` — version 4 → **5**

Constant: `FB2_SECTION_FILE_VERSION` (`lib/Fb2/Fb2/Fb2Section.cpp:20`).

Layout is **unchanged**. The bump is required because the file's *name* is the chapter
index and chapter indices now mean something different: a version-4 `sections/1.bin` holds
pages of the old "second top-level section" and would silently render the wrong chapter.
Existing mismatch handling rebuilds the file.

## `progress.bin` — 6 bytes → **8 bytes**

Encoded and decoded in `src/activities/reader/Fb2ReaderMath.*`; written atomically by
`ProgressFile::writeAtomic`.

```text
u16  chapter      # chapter index (v2) / top-level ordinal (legacy payloads)
u16  page
u16  pageCount
u16  marker       # 0xFB02, present only in the 8-byte form            <-- NEW
```

**Decode contract** (`fb2_reader::decodeProgress`)

| Payload size | Interpretation |
|--------------|----------------|
| 8 with `marker == 0xFB02` | `chapter` is a chapter index; `page`, `pageCount` as today |
| 8 with any other marker | invalid (`valid = false`) |
| 6 | legacy: `chapter` is a top-level ordinal, `pageCount` present but not trusted, page forced to 0 |
| 4 | legacy: as above, no page count |
| anything else | invalid (`valid = false`) — unchanged |

The struct gains one flag (`legacyOrdinal`) so the caller knows to map the value through
`Fb2::firstChapterOfTopLevel()` and reset the page. Writing always emits the 8-byte form,
so a book migrates on its first save. Out-of-range chapter values keep being clamped by
`Fb2ReaderActivity`'s existing bounds checks (`Fb2ReaderActivity.cpp:333-335`).

XTC and TXT readers share `ProgressFile` but not this payload; their decoders are
untouched (`XtcReaderMath`, `TxtReader*`), so the 8-byte form must stay FB2-local.
