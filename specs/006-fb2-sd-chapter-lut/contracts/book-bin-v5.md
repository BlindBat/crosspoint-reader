# Contract: FB2 `book.bin` version 5

This replaces the version 4 entry in `docs/file-formats.md` (`## FB2 caches`). That doc is
updated in the same commit as the version bump (FR-009). Fields are little-endian, as all
existing caches are. Every read goes field by field through `readPodChecked`, never by casting
a buffer (RISC-V alignment).

```c++
u8     version;         // 5
String title;           // u32 length + bytes, ≤ 4096
String author;
String language;
String coverBinaryId;
u16    chapterCount;    // 1..65535
u32    titlesSize;      // bytes in the title area
// recordsOffset = position here
Record record[chapterCount];   // 20 bytes each, chapter order (start-tag order)
// titlesOffset = recordsOffset + chapterCount * 20
u8     titles[titlesSize];     // raw title bytes, no prefixes, no terminators

struct Record {         // 20 bytes, no padding: written field by field
  u32 titleOffset;      // into titles[]
  u16 titleLength;      // ≤ 4096; 0 = no title (UI shows "Unnamed")
  u32 fileOffset;       // offset of the section's "<section" start tag in the source
  u32 ownLength;        // own bytes: full span minus child chapters' spans
  u32 cumulativeLength; // sum of ownLength over records 0..i
  u8  level;            // nesting depth; 0 = direct child of <body>
  u8  flags;            // bit0: title derived from first paragraph; others 0
};
```

## Guarantees

- **Constant-time lookup**: record *i* is at `recordsOffset + 20·i`. A lookup is one seek and
  a 20-byte read, plus one seek and a `titleLength`-byte read for the title.
- **Truncation is caught at open**: `fileSize == titlesOffset + titlesSize` exactly.
- **Progress is one record**: chapter *i* spans the bytes `[cumulativeLength − ownLength,
  cumulativeLength)`. Book size is the last record's `cumulativeLength`.
- The validation rules are listed in [data-model.md](../data-model.md#validation-rules-enforced-at-load-any-failure-rejects-the-file-and-the-book-is-re-parsed).
  A failed check means re-parse the source, never trust the file.

## Migration

- A version 4 (or older) file is rejected by the version byte. The book is re-parsed once.
- If the fresh parse has more than 256 chapters, `sections/` is removed too, because those
  layouts were built under capped boundaries (research R6).
- `progress.bin` does not change (`u16 chapterIndex`, 8-byte payload).

## Build-time temp files (in the book's cache dir, removed on success and on failure)

| File | Content |
|---|---|
| `chapters.tmp` | `Record[]`: slot appended at start tag, patched at end tag; `titleOffset` is relative to `titles.tmp` and `cumulativeLength` is 0 until assembly |
| `titles.tmp` | title bytes appended at end tags |
