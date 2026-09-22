# Quickstart: validating FB2 chapter metadata on SD

## Host (gates 2–3)

```bash
bin/run-tests            # plain
bin/run-tests --asan     # ASan + UBSan
```

These tests need the following results:

| Check | Suite | Expected |
|---|---|---|
| Chapter memory is independent of count (SC-002) | `fb2_book` | a 4-chapter and a 4,000-chapter generated book of the **same depth** hold the same live bytes after `load()`, and the same peak during first indexing. The allowance is 0. |
| No cap below the format limit (SC-001, FR-001) | `fb2_book`, `fb2_metadata_parser` | a 4,000-section tower yields 4,000 chapters, and each chapter's text is its own |
| Partition and monotonic progress (FR-008) | `fb2_book` | the sum of `length` equals the last `cumulativeLength`; `calculateProgress` never decreases |
| Parsers stay in lockstep (FR-005) | `fb2_section_cache` | the chapter at index 3,000 of a tower lays out its own text |
| v5 malformed corpus (FR-011) | `fb2_book` | each fixture below is rejected and the book is re-parsed |
| Old layouts dropped only for over-cap books (FR-010, SC-006) | `fb2_book` | v4 `book.bin` with ≤256 chapters: `sections/` survives. v4 with >256: `sections/` is removed |
| Zero index reads per page turn (SC-004) | `fb2_book` | after `chapterInfo` is filled, `calculateProgress` does no I/O (it takes the cached record, so no index is passed — asserted by an open counter added to the shared stub) |
| Binary-search percent jump | `xtc_fb2_readers` (`Fb2ReaderMathTest`) | same targets as before; call count ≤ ⌈log₂ n⌉+2 |

The malformed-input fixtures are generated in the test, deterministically:
- a truncated title area;
- `titlesSize` off by one;
- `titleOffset` past the area;
- `cumulativeLength` wrong;
- a level jump;
- a stray flag bit;
- `chapterCount = 0`;
- `chapterCount = 65535` in a small file.

Per Constitution V, show at least one production mutation each new test catches. Examples:
drop the `titlesSize` equality check; stop removing `sections/`.

## Corpus (SC-001)

Rebuild the throwaway tool from `research.md` R1 against the **real** `lib/Fb2` (no patch
needed now) and run it over `~/Calibre Library`. Expected: 2,899 books load. The 1,772-chapter
book lists 1,772 chapters. The chapter count per book matches the R1 table.

## Device (gate 6, human)

Flash `default` to the X4 and capture serial to a file (see `device-measurement` notes: reset
the board first so `Min Free` is fresh).

1. **SC-003 heap**: open the 1,772-chapter book from the corpus. `[MEM] Free` after open should
   be ≥ the value measured on current firmware for the worst book at the 256 cap (current
   firmware holds 43,732 B of chapter metadata for it). Record both numbers.
2. **SC-005 chapter list**: in that book, page through the chapter list from top to bottom.
   Time each window refresh (`LOG_DBG` around `refreshTocWindow`) and compare with an EPUB of
   ~1,000 TOC entries. The FB2 refresh should take no longer. `[MEM] Free` should stay flat.
3. **First-open cost (research R2)**: delete `fb2_<hash>/book.bin` for the reference
   anthology and time `Fb2::load()`, once on current firmware and once on this branch, three
   runs each. If this branch is slower by more than the run-to-run spread, buffer `titles.tmp`
   writes (the `ponytail:` upgrade path).
4. **Migration (US3)**: with the previous firmware, read into one book with ≤256 chapters and
   one with >256, then flash this branch. The first book reopens at the same page with no
   "Indexing" popup. The second reopens at the same chapter, and its chapters lay out afresh.
5. **Status bar**: set the title mode to chapter title and read past chapter 256 of the large
   book. The title shown should be that chapter's own.
