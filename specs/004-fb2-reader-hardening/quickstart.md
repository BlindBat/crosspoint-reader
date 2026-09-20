# Quickstart: Validating FB2 Reader Hardening

Prerequisites: the host toolchain `bin/run-tests` needs (`CC`/`CXX` pointed at a working
clang), and PlatformIO for the device build. Nothing here needs hardware except the two device
checks at the end, which are the human tester's.

## 1. Host suites — the memory and label rules

```bash
bin/run-tests --filter 'fb2'          # fb2_book, fb2_metadata_parser, fb2_section_*, fb2_cover_extractor
bin/run-tests --filter 'fb2' --asan   # same, under ASan + UBSan
```

Expect, once the feature is built:

| What it proves | Where |
|---|---|
| SC-002 — chapter metadata stays flat past the ceiling and inside the retuned budget (device model: 36 B/chapter + one bounded title block; worst real book 43,732 B) | `Fb2BookTest.ChapterMetadataAllocatesOneTitlePerChapterAndStaysCapped`, budgets retuned to the new ceiling |
| FR-003 / SC-001 — a book past the ceiling still opens and loses no text | `Fb2BookTest.ChapterCountIsCappedWhenParsingAHugeBook` |
| FR-006 — an over-ceiling or corrupt `book.bin` is rejected before `reserve()` | `Fb2BookTest.ChapterCountAboveTheCapIsRejectedWithoutReserving` and its siblings |
| L1–L8 — label derivation | new cases in the `fb2_metadata_parser` and `fb2_book` suites |
| v4 round trip, and a v3 cache rejected and rebuilt | new case in `fb2_book` |
| SC-008 — malformed `flags`, truncated strings, lying lengths | `fb2_book` corrupt-cache cases, plus ASan clean |

The full program must pass, not just the filter, before anything merges:

```bash
bin/run-tests && bin/run-tests --asan
```

## 2. Format and static analysis

```bash
./bin/clang-format-fix -g
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

## 3. Device build

```bash
pio run -e default          # ESP32-C3 — the target the ceiling is sized for
```

A change that builds on C3 can still fail on an S3 board; CI covers `sticky`, `x4pro`, `x4c`
and `papermono`.

## 4. Simulator — the parts no host test reaches

The chapter list and the cover page are rendering, so they are verified by running the firmware,
not by the suites. Use the `run-simulator` skill (it handles the toolchain, a per-branch SD card
and the version stamp).

Put a nested FB2 anthology with a cover on the simulator's SD card, then walk:

1. Open the book with no cache present → it opens **on the cover** (V1). One forward turn →
   first page of text (V4). One back turn → the cover again (V4).
2. Open the reader menu from the cover → chapter 1 of *n*, and a progress percent of 0 (V5).
3. Open the chapter list → every row with text of its own shows words, not `Unnamed` (SC-007,
   L2); rows with neither title nor text still show the placeholder (L7); indentation by nesting
   level is unchanged.
4. Scroll the chapter list from the first row to the last and back → no stutter, no missing
   rows, selection opens the chapter the row names (FR-005, L8).
5. Select a chapter in the middle, back out of the book, reopen it → it lands on that chapter,
   **not** on the cover (V3).
6. Delete the book's `cover.bmp` from `/.crosspoint/fb2_<hash>/` and reopen → the book opens on
   its first page of text, no blank page (V2).

## 5. Position compatibility (the check that protects existing readers)

Before flashing the change, open a book on the current firmware and leave it mid-chapter. After
flashing:

- The book reopens at the same chapter and the same page (FR-011, SC-005).
- Its `sections/<n>.bin` files are untouched — the first page after reopening renders at cached
  speed (~7 ms), not after a rebuild (SC-006 as narrowed in plan.md).
- Its `book.bin` is rewritten once, on that first open (Decision 5).

## 6. Re-running the corpus measurement (optional)

The ceiling's justification is reproducible. Build a small tool against `lib/Fb2` + the
`test/fb2_common` stubs, walk a directory of real `.fb2` files, and for each book sum
`chapters × 36 + Σ(len > 15 ? align4(len+1) + 8 : 0)` over `getSectionInfo(i).title`. Compare
the worst result to the usable heap from a `pio run -e default` RAM report minus the 48 KB
framebuffer. Method and the 2,899-book result: [research.md](research.md) M1–M3.

## 7. Hardware (human tester)

- Free heap after opening the reference anthology, via serial: compare to the pre-change figure;
  the chapter list should no longer scale with chapter count.
- The cover page in all four orientations.
- A cover rendered in black and white only — confirm the dithered result is acceptable, or file
  the grayscale pipeline as a follow-up (research.md Decision 6 records it as deliberately
  skipped).
