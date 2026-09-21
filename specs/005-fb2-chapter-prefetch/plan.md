# Implementation Plan: FB2 Next-Chapter Prefetch

**Branch**: `feature/fb2-chapter-prefetch` | **Date**: 2026-09-21 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/005-fb2-chapter-prefetch/spec.md`

## Summary

Lay out the next FB2 chapter's pages while the reader is idle, so crossing the
boundary finds a finished cache file instead of a four-second build.

The whole feature reduces to one missing capability plus one caller. `Section`
(EPUB) is already resumable — `startBuild()` / `buildSomeMore(N)` /
`isBuildComplete()` ([Section.h:91-99](../../lib/Epub/Epub/Section.h)) — and
`EpubReaderActivity::loop()` already drives it in two-page slices under a heap gate
and a `RenderLock`
([EpubReaderActivity.cpp:390-403](../../src/activities/reader/EpubReaderActivity.cpp)).
`Fb2Section` has none of that: `createSectionFile()` runs the whole parse in one
blocking call ([Fb2Section.cpp:163-220](../../lib/Fb2/Fb2/Fb2Section.cpp)).

So:

1. **Make the FB2 parse resumable.** `Fb2SectionParser`'s feed loop
   ([Fb2SectionParser.cpp:466-493](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)) is already
   a `read 1024 bytes → XML_ParseBuffer` cycle. Promote `XML_Parser` and the `HalFile`
   from locals to members and split the loop into `beginParse()` / `parseSome(budget)` /
   `finishParse()`. Pausing needs no expat suspend API — we own the feed, so we simply
   stop calling it.
2. **Mirror EPUB's build API onto `Fb2Section`.** `startBuild` / `buildSomeMore` /
   `isBuilding` / `isBuildComplete` / `abandonBuild`, over a heap `BuildContext` holding
   the parser and the page LUT. `createSectionFile()` stays as the one-shot wrapper, so
   every existing caller and test is untouched.
3. **Drive it for the chapter ahead.** `Fb2ReaderActivity` gains a `loop()` override
   that calls the base and then one `prefetchTick()`, holding a second `Fb2Section` for
   chapter *N+1*.

**No handover.** Prefetch's only product is a valid `sections/<N+1>.bin` on the card.
When the reader crosses the boundary it constructs its own `Fb2Section` and calls
`loadSectionFile()` exactly as today ([Fb2ReaderActivity.cpp:408-425](../../src/activities/reader/Fb2ReaderActivity.cpp)) —
the file is simply already there, and loads in the measured ~7 ms. No object is passed
between the two, no lifetime is coupled, and the reader's own path is unchanged.

**No new cache format.** Prefetch writes byte-identical output to an on-demand build, so
`FB2_SECTION_FILE_VERSION` does not move and `docs/file-formats.md` needs no edit. A
build in progress writes to `sections/<N+1>.bin.part` and is swapped over the real path
only on completion, so an abandoned prefetch leaves nothing a reader can mistake for a
cache (mirroring `Section::binTmpPath()` and `commitBuildFile`'s remove-then-rename —
FAT refuses a rename onto an existing path, see [contracts/build-api.md B4](contracts/build-api.md)).

**No new task, no new dependency, no new UI, no new setting.**

### Two findings that shaped the design

**The prefetch is free in aggregate.** `Fb2SectionParser` parses every chapter from
byte 0 of the book — expat cannot start mid-document, so the parser scans forward to the
target section ([Fb2SectionParser.cpp:459-460](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)).
That is true of today's on-demand build too. Prefetching chapter *N+1* therefore performs
the same scan the reader would have performed anyway, just earlier. The only new cost is
a prefetch that gets abandoned. This is what makes the feature cheap enough to justify.

**Prefetch cannot resume across a power cycle, so FR-015 is partly unmet.** See
[Spec Deviations](#spec-deviations).

**Vocabulary**: the spec says *preparation* and *the chapter ahead* because it avoids
implementation words; this plan, the contracts and the tasks say *prefetch* and *chapter
N+1* for the same things.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), `-fno-exceptions`, no RTTI

**Primary Dependencies**: in-tree expat (`XML_GE=0`, `XML_CONTEXT_BYTES=1024`),
`HalStorage`, `GfxRenderer` (text metrics only), the shared EPUB text pipeline
(`ParsedText` / `Page` / `BlockStyle`), `RenderLock`

**Storage**: SD via `HalStorage`. Per-book cache `.crosspoint/fb2_<hash>/sections/<n>.bin`,
plus a transient `<n>.bin.part` during a build. No format or version change.

**Testing**: host gtest via `bin/run-tests` (plain + `--asan`). Existing suites extended:
`fb2_section_cache`, `fb2_section_parser`, `xtc_fb2_readers` (target predicate),
`alloc_guards` (allocation-count proxy). The host storage stub's `rename` must be made to
refuse an existing destination first, or the swap tests pass on host and fail on device.
New device/simulator verification for timing and heap.

**Target Platform**: ESP32-C3 (`default`, ~380 KB RAM, single core, no PSRAM) is the
binding target; ESP32-S3 boards inherit. Interaction verified in crosspoint-simulator,
timing and heap on hardware.

**Project Type**: e-reader firmware — portable reader core in `lib/`, UI in `src/activities/`

**Performance Goals**: a first-time forward chapter crossing completes in the same order
as an in-chapter page turn. Baseline from
[issue #4](https://github.com/BlindBat/crosspoint-reader/issues/4): ~4 s to build a
42-page chapter on demand, 7 ms to load one already cached (Xteink X4 / ESP32-C3,
1.9 MB FB2, 66 chapters). No regression in in-chapter page-turn latency.

**Constraints**: one application task only; a slice runs under `RenderLock`, so slice
duration is a direct tax on render latency. Three numbers — slice page budget, slice byte
budget, idle settle interval — are deliberately unset here and fixed by measurement in
Phase 0 tasks R1–R3 ([research.md](research.md)).

**Scale/Scope**: five source files (`Fb2SectionParser.{h,cpp}`, `Fb2Section.{h,cpp}`,
`Fb2ReaderActivity.{h,cpp}`, `Fb2ReaderMath.{h,cpp}` for the target predicate) plus four
test targets (`fb2_section_cache`, `fb2_section_parser`, `xtc_fb2_readers`, `alloc_guards`)
and the host storage stub. No change to EPUB, TXT, XTC, or any cache format.

## Constitution Check

*GATE: passed before Phase 0; re-checked after Phase 1 design — see [Post-Design Re-check](#post-design-re-check).*

| Principle | Assessment |
|-----------|------------|
| **I. A Focused Reading Device** | PASS. Removing a four-second stall at every chapter boundary is the core reading experience. No new surface: no setting, no screen, no connectivity. The feature exists because nested-chapter navigation (003) redistributed pagination work into many small waits. |
| **II. Memory Is the Design Constraint** | PASS with a budget. One additional live `Fb2SectionParser` + expat instance + open `HalFile` + one in-flight `ParsedText`/`Page`. `BuildContext` is `makeUniqueNoThrow`; the page LUT gets `.reserve()` (research R4); prefetch is gated on free heap and largest block before it starts and pauses when either falls, reusing the reader's existing 32 KB / 16 KB thresholds ([EpubReaderActivity.h:110-111](../../src/activities/reader/EpubReaderActivity.h)) unless R4 measures that FB2 needs more. Peak live set is *lower* than the status quo's, because today's on-demand build holds the same objects — prefetch just holds them at a different time. |
| **III. Portability Behind the HAL** | PASS. All new logic is in `lib/Fb2/`, which is already host-compilable and reached by two gtest suites. The `src/` changes are the activity that drives it plus a pure target-selection predicate in `Fb2ReaderMath`, which is itself host-compiled by `xtc_fb2_readers`. No Arduino or hardware include crosses the seam. |
| **IV. Evidence Over Claims** | PASS. Every claim cites a file/line or the issue's device log. Three numeric parameters are named but left unset, with a measurement procedure each (R1–R3); none is written down as a round number with a story. The "prefetch is free in aggregate" claim states its mechanism (the scan already happens per chapter today, [Fb2SectionParser.cpp:459-460](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)). |
| **V. Tests Prove Behavior** | PASS. The load-bearing test is **incremental output ≡ one-shot output** for the same chapter and spec, mirroring [EpubSectionBuildTest.cpp:170](../../test/epub_section/EpubSectionBuildTest.cpp). Slice-boundary tests cover pausing mid-page, mid-tag and mid-text-node. Each new suite must name a production mutation it catches ([contracts/build-api.md](contracts/build-api.md) §T). |
| **VI. Untrusted Input Is Hostile** | PASS. Prefetch reads the same bytes through the same parser as an on-demand build, so the existing malformed-input corpus covers it. The one new surface is the `.part` file: it is never read back, is removed on abandon, and becomes visible to a reader only by the remove-then-rename swap after the LUT and page count are written ([contracts/build-api.md B4](contracts/build-api.md) — FAT cannot rename onto an existing path, so the swap is not atomic and copies `Section::commitBuildFile`'s sequence). An abandoned in-place write is already rejected by `loadSectionFile`'s extent check ([Fb2Section.cpp:124-136](../../lib/Fb2/Fb2/Fb2Section.cpp)); `.part` means that path is not exercised at all. |
| **VII. Upstream-First Fork Hygiene** | PASS. On `feature/fb2-chapter-prefetch`, not `master`. Issue #4 is the fork's own, opened from hardware verification; no upstream PR covers it (checked at spec time). The `lib/Fb2/` change is self-contained and cherry-pickable; host suites stay fork-only. Non-test diff is expected to sit near the 200-line guidance — see [Complexity Tracking](#complexity-tracking). |

## Project Structure

### Documentation (this feature)

```text
specs/005-fb2-chapter-prefetch/
├── plan.md              # This file
├── research.md          # Phase 0: decisions, rejected alternatives, measurement tasks R1-R5
├── data-model.md        # Phase 1: build state machine, prefetch state machine, lifetimes
├── quickstart.md        # Phase 1: how to verify, host + simulator + device
├── contracts/
│   ├── build-api.md     # Normative Fb2Section incremental-build contract + test obligations
│   └── prefetch-policy.md  # When prefetch may run, must pause, and must stand down
├── checklists/
│   └── requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks) - NOT created here
```

### Source Code (repository root)

```text
lib/Fb2/Fb2/
├── Fb2SectionParser.h     # XML_Parser + HalFile + feed cursor become members
├── Fb2SectionParser.cpp   # parseAndBuildPages() splits into begin/parseSome/finish
├── Fb2Section.h           # + BuildContext, startBuild/buildSomeMore/isBuilding/
│                          #   isBuildComplete/abandonBuild, binTmpPath()
└── Fb2Section.cpp         # createSectionFile() becomes the one-shot wrapper;
                           #   commit = write LUT, patch header, rename .part over .bin

src/activities/reader/
├── Fb2ReaderActivity.h    # + prefetch section, prefetch target index, idle timestamp,
│                          #   prefetchTick(), loop() override
├── Fb2ReaderActivity.cpp  # loop() { ReaderActivity::loop(); prefetchTick(); }
└── Fb2ReaderMath.{h,cpp}  # + the pure target-selection predicate (host-reachable)

test/
├── fb2_section_cache/     # + incremental-equivalence, slice boundaries, abandon/.part
├── fb2_section_parser/    # + resume-across-slices parity with one-shot
├── xtc_fb2_readers/       # + target-predicate tests (Fb2ReaderMathTest.cpp)
├── alloc_guards/          # + sliced-build allocation proxy
└── fb2_common/stubs/      # HalStorage rename must refuse an existing destination
```

**Structure Decision**: unchanged layout. The resumable machinery lives in the
host-compilable reader core (`lib/Fb2/`), the scheduling policy lives in the activity
(`src/activities/reader/`), matching how EPUB already splits the same concern between
`Section` and `EpubReaderActivity`.

## Spec Deviations

**FR-015 (retain partially completed preparation across exit, sleep and navigation) is
met only for completed prefetch.**

Resuming an interrupted FB2 layout would mean restarting the parse mid-document, which
expat cannot do: the parse must begin at byte 0 of a well-formed document
([Fb2SectionParser.cpp:459-460](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)). EPUB can retain
partial builds only because its parser starts at the chapter's own extracted HTML file, so
a byte watermark is a resumable position ([Section.h:61-67](../../lib/Epub/Epub/Section.h));
FB2 has no such file. Retaining FB2 work would require either persisting expat's internal
state (not exposed) or synthesising a prologue and seeking to the section offset (fragile
against encodings, entity declarations and namespace scope — a correctness risk on
untrusted input, against Principle VI).

**What is delivered instead**: a *completed* prefetch persists as an ordinary cache file
and is never redone (US3's actual user value — FR-015 acceptance scenario 1's outcome for
the common case). An *in-flight* prefetch is discarded and its `.part` removed. This is not
a regression: today the same interruption discards the same work.

**Why this is the right trade**: the interruption window is small. On the reference book a
chapter is ~15 pages and prefetch has the whole time the reader spends reading the current
chapter — minutes against seconds of work. FR-015's P2 rating in the spec already says US1
delivers value without it.

**If you want FR-015 in full**, it is a separate feature: give FB2 chapters their own
extracted-text intermediate file the way EPUB has extracted HTML, which then makes both
byte-resumable *and* removes the per-chapter full-file rescan. That is a larger change than
this one and belongs with issue #8's cache rework, not here.

## Complexity Tracking

| Item | Why needed | Simpler alternative rejected because |
|------|-----------|--------------------------------------|
| Resumable parse (`beginParse`/`parseSome`/`finishParse`) rather than calling the existing one-shot `createSectionFile()` from idle | A one-shot call blocks for ~4 s (issue #4), which trips the watchdog, freezes input and holds `RenderLock` — violating FR-007, FR-008, FR-009 | There is no way to bound a blocking call's duration from outside it |
| A *second* slice budget (bytes fed) alongside the page budget EPUB uses | FB2 scans from byte 0 to the target section emitting **zero** pages ([Fb2SectionParser.cpp:459-460](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)). A page-only budget would run ~1,900 buffer reads without yielding on a late chapter of the reference book | EPUB needs only a page budget because its parser starts inside the chapter; copying that alone would make late chapters block for seconds |
| `.part` file + rename instead of writing `sections/<N+1>.bin` in place | An abandoned in-place build leaves a header-only file at the real cache path | It would be *correct* (`loadSectionFile`'s extent check rejects `lutOffset == 0`, [Fb2Section.cpp:124-136](../../lib/Fb2/Fb2/Fb2Section.cpp)) but relies on a validation path for ordinary operation rather than for hostile input, and burns a delete + full rebuild on every interruption. `Section::binTmpPath()` sets the precedent |
| Non-test diff may approach the 200-line upstream guidance (Principle VII) | The parser split and the `Fb2Section` build API are one logical change — the API is unusable without the split, and the split has no caller without the API | Splitting into two PRs would land dead code in the first one. The activity change (~40 lines) is separable and SHOULD be a second commit so the core is individually cherry-pickable |

### Deliberate non-goals (ladder stops)

- **No `std::function`, no new callback type.** The existing `Fb2PageCompleteFn` /
  `BuildPopupFn` context-pointer pairs carry everything
  ([ReaderCallbacks.h](../../lib/Epub/Epub/ReaderCallbacks.h)).
- **No new virtual on `ReaderActivity`.** `Fb2ReaderActivity` overrides `loop()` and calls
  the base. A `virtual void onIdleTick()` seam with exactly one implementation is an
  abstraction the feature does not need; add it when a second reader wants prefetch.
- **No prefetch object handover at the boundary.** The cache file is the interface.
- **No user-facing toggle.** Recorded as an assumption in the spec.
- **No continuation of the current chapter's parse into the next.** Rejected in
  [research.md R5](research.md) — it would hold a parser and an open file for the whole
  time the user reads a chapter, and it breaks for parent chapters whose next chapter is
  their own first child.

## Post-Design Re-check

Re-evaluated after Phase 1 artifacts. No gate moved.

- **II (memory)**: the design adds no allocation the status quo does not already make; it
  relocates them in time. `data-model.md` enumerates the live set; R4 measures it. Gate
  values reused rather than invented.
- **IV (evidence)**: three parameters remain unset with named procedures (R1–R3). The plan
  asserts no timing improvement beyond the issue's own measured baseline.
- **V (tests)**: `contracts/build-api.md` §T lists the mutation each new suite must catch,
  so no suite can be written that passes against broken code.
- **VI (untrusted input)**: the `.part` file is write-only and never parsed; no new
  deserialization surface exists.
- **Corrected after `/speckit-analyze`**: the first draft claimed the `.part` was swapped
  into place by a single atomic `Storage.rename()`. `FatFile::rename` opens the destination
  `O_CREAT | O_EXCL | O_WRONLY`, so it fails on an existing path; the swap is
  remove-then-rename, as `Section::commitBuildFile` already does. The host stub's POSIX
  `rename` overwrites silently and hid the difference, so the stub is fixed first (T017).
  `Storage.rename` existing in the HAL was not evidence that it replaces an existing file.
- **Spec conformance**: FR-001 to FR-014 and FR-016 to FR-020 are met by the design;
  FR-015 is partially met and the gap is documented above rather than silently dropped
  (Constitution Governance: "silent exceptions are not permitted").
