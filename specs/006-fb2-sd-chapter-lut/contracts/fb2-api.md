# Contract: `Fb2` chapter lookup API

Only the chapter accessors change. Every other `Fb2` method keeps its signature.

```c++
class Fb2 {
 public:
  // Highest chapter the UI list can address: its row value and selection index are
  // int16_t (FreeInkUI lists/list.h). Not a memory budget. Past it a nested <section>
  // reads as part of the chapter containing it; a top-level one is unreachable.
  static constexpr uint16_t FB2_CHAPTER_INDEX_LIMIT = INT16_MAX;   // replaces FB2_MAX_CHAPTERS

  int getSectionCount() const;                       // unchanged; from the header

  // By value. An out-of-range index, or any read failure, returns an empty
  // SectionInfo, which the UI shows as "Unnamed" (FR-012).
  SectionInfo getSectionInfo(int index) const;       // opens book.bin itself
  SectionInfo getSectionInfo(int index, HalFile& bookBin) const;  // caller-opened handle, for batches
  bool openIndex(HalFile& out) const;                // opens book.bin for reading

  size_t getBookSize() const;                         // from memory, no I/O
  size_t getCumulativeSectionSize(int index) const;   // one record read
  size_t getCumulativeSectionSize(int index, HalFile& bookBin) const;
  float calculateProgress(const SectionInfo& chapter, float chapterRead) const;  // no I/O

  // TOC is still the identity projection of the chapter list.
  int getTocCount() const;
  SectionInfo getTocEntry(int index) const;
  SectionInfo getTocEntry(int index, HalFile& bookBin) const;
  int getTocIndexForSectionIndex(int) const;           // unchanged
  int getSectionIndexForTocIndex(int) const;           // unchanged

  int firstChapterOfTopLevel(int ordinal) const;       // one sequential pass, legacy progress only
};
```

## Caller obligations (FR-013)

| Caller | Before | After |
|---|---|---|
| `Fb2ReaderActivity::renderStatusBar` | a `c_str()` taken from a returned reference | reads `chapterInfo.title`, the reader-owned copy (research R4) |
| `Fb2ReaderActivity::bookProgressPercent` / status bar | `calculateProgress(index, f)` | `calculateProgress(chapterInfo, f)` |
| `Fb2ReaderActivity::jumpToPercent` | a callback per index | opens the index once; the callback context holds the `HalFile` |
| `Fb2ReaderChapterSelectionActivity::refreshTocWindow` | `const auto&` into the vector | one `openIndex`, then up to 24 lookups with the handle |
| `Fb2Section::createSectionFile` | `const auto&` | `const auto` (by value); one lookup |

No caller may keep a pointer or reference into a returned `SectionInfo` past the full
expression that holds it, unless it owns the copy.

## Parser sink

```c++
struct Fb2ChapterSink {
  void* ctx;
  bool (*reserve)(void* ctx);                                           // start tag: new slot
  bool (*write)(void* ctx, uint16_t index, const Fb2::SectionInfo& c);  // final fields
};
```

A `false` return stops the parse (`XML_StopParser`), and `parse()` then returns false.
`Fb2SectionParser` counts chapters in lockstep against the same `FB2_CHAPTER_INDEX_LIMIT`
(FR-005).

## `fb2_reader::percentToSection`

The signature and result are unchanged. Internally it becomes a lower-bound binary search over
`cumulative(i)`, making ⌈log₂ n⌉+1 callback calls instead of up to n.
