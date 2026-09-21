# Phase 0 Research: FB2 Next-Chapter Prefetch

Decisions taken before design, the alternatives rejected, and the five measurements
that must be made before the numbers in this feature are written down (Constitution IV:
a numeric limit MUST cite the measurement that chose it).

---

## D1 — Where prefetch runs: the existing loop, not a new task

**Decision**: prefetch runs as bounded slices inside `Fb2ReaderActivity::loop()`, under a
`RenderLock` taken per slice.

**Rationale**: the firmware creates exactly one FreeRTOS task of its own — the shared
render task in `ActivityManager::begin()`
([ActivityManager.cpp:32-46](../../src/activities/ActivityManager.cpp)). The EPUB reader
already does background page layout this way: gated on `!RenderLock::peek()` and a heap
check, then a `RenderLock` around a two-page slice
([EpubReaderActivity.cpp:390-403](../../src/activities/reader/EpubReaderActivity.cpp)).
The lock is not optional: `loop()` runs on the Arduino task while `render()` runs on the
render task, so layout and rendering genuinely race for the renderer's font cache.

**Alternatives considered**:

- *A dedicated prefetch task.* Rejected. The C3 is single-core, so a second task cannot
  add throughput — it only adds a stack (≥4 KB), preemption at arbitrary points inside
  the parser, and contention on `HalStorage`'s recursive mutex, which is per-task and
  would no longer be re-entered by the same task. The constitution's Platform Constraints
  forbid reader-core code that depends on multi-threading. Issue #4 raises this as its
  first design question; this is the answer.
- *Prefetch from the render task after a render completes.* Rejected. It would hold the
  render task for the slice duration and starve the very thing the lock protects.

---

## D2 — How the parse is sliced: we own the feed, so we just stop feeding

**Decision**: promote `XML_Parser` and the input `HalFile` from locals to
`Fb2SectionParser` members, and split `parseAndBuildPages()` into `beginParse()` /
`parseSome(pageBudget, byteBudget)` / `finishParse()`. `parseAndBuildPages()` remains as a
one-shot wrapper calling all three.

**Rationale**: the existing feed loop is already a pull cycle we control —
`XML_GetBuffer(1024)` → `file.read` → `XML_ParseBuffer`
([Fb2SectionParser.cpp:466-493](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)). Stopping between
iterations needs no expat API at all; `XML_StopParser`/`XML_ResumeParser` are for stopping
*inside* a handler and are not needed. All parser state (`depth`, `chapterCount`,
`inTargetSection`, `blockStyleStack`, `currentPage`, `partWordBuffer`) is already member
state that survives between `XML_ParseBuffer` calls.

**Alternatives considered**:

- *`XML_StopParser(parser, XML_TRUE)` from inside the page-complete handler.* Rejected as
  strictly more machinery for the same effect, and it complicates the error path
  (`XML_ERROR_SUSPENDED` becomes a non-error status to distinguish).
- *Keeping the one-shot call and running it during a long idle.* Rejected: ~4 s of
  blocking (issue #4) trips the watchdog, freezes input, and holds the render lock.

---

## D3 — Two slice budgets, not one

**Decision**: a slice ends when **either** `pageBudget` pages have been emitted **or**
`byteBudget` bytes have been fed to expat.

**Rationale**: this is the one place FB2 cannot copy EPUB. `Fb2SectionParser` parses every
chapter from byte 0 of the book, because expat needs a well-formed document from the start
([Fb2SectionParser.cpp:459-460](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)) — the handlers
return early until the target section is reached
([Fb2SectionParser.cpp:147-148, 254](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)). Building a
late chapter therefore emits **zero** pages for most of its run. With a page-only budget
(EPUB's shape), a slice targeting chapter 60 of the reference book would run roughly 1,900
buffer reads before its first page and block for seconds while holding `RenderLock`. The
byte budget is what bounds slice duration; the page budget bounds the layout work once
pages start arriving.

**Consequence worth stating plainly**: prefetching chapter *N+1* re-scans the file from
byte 0. That is **not new cost** — today's on-demand build of chapter *N+1* performs
exactly the same scan when the reader crosses the boundary. Prefetch moves it, it does not
add it. The only genuinely new cost is a prefetch that is abandoned before completing.

---

## D4 — Prefetch's product is a cache file, not an object

**Decision**: prefetch builds `sections/<N+1>.bin` and nothing else. When the reader
crosses the boundary it constructs its own `Fb2Section` and calls `loadSectionFile()`
unchanged ([Fb2ReaderActivity.cpp:408-425](../../src/activities/reader/Fb2ReaderActivity.cpp)),
which finds the file and loads it in the measured ~7 ms.

**Rationale**: the cache file is already the interface between a build and a read. Handing
the prefetched `Fb2Section` object over would couple two lifetimes, require the reader to
decide whether the handed-over object matches its current render spec, and add a state the
existing code has no concept of — all to save one `loadSectionFile()` call that costs 7 ms
on device.

**Alternatives considered**:

- *Hand the object over on a boundary crossing.* Rejected as above.
- *Prefetch into memory only, no file.* Rejected: a laid-out chapter does not fit
  comfortably in RAM on a 380 KB device, and the file is needed anyway for the next open.

---

## D5 — Not continuing the current chapter's parse into the next

**Decision**: each prefetch starts its own parse. The parser that built chapter *N* is
destroyed when chapter *N*'s build completes, as it is today.

**Rationale**: continuing looks tempting — when the build of chapter *N* stops, the parser
is positioned right at the end of *N*, so feeding it onward would reach *N+1* without
re-scanning. Three reasons not to:

1. **It is wrong for parent chapters.** With nested chapters (feature 003), chapter *N+1*
   may be chapter *N*'s own first child, whose bytes are *inside* *N*'s subtree.
   `pastTargetSection` is set at the target section's **end** tag
   ([Fb2SectionParser.cpp:344-346](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)), i.e. after its
   children. So for any parent chapter the next chapter's bytes are already behind the
   cursor, and continuation silently produces the wrong chapter. Supporting both cases
   means comparing `sections[N+1].fileOffset` against the cursor and branching — a state
   machine for a partial win.
2. **It holds memory for minutes, not seconds.** Continuation requires keeping the parser,
   the expat instance and an open `HalFile` alive for the whole time the user reads chapter
   *N*. The design in D1 holds them only during slices.
3. **It optimises something the spec puts out of scope.** The spec says the feature moves
   cost off the critical path and does not make the work cheaper. During idle, under a
   byte budget, the re-scan is invisible.

**Recorded as the upgrade path**: if the re-scan ever becomes the binding cost, the fix is
not continuation but giving FB2 chapters an extracted-text intermediate file, the way EPUB
has extracted HTML. That would make builds byte-resumable (closing the FR-015 gap in the
plan's Spec Deviations too) and belongs with issue #8's metadata rework.

---

## D6 — `.part` file and the remove-then-rename swap

**Decision**: a build writes to `sections/<n>.bin.part` and renames it over
`sections/<n>.bin` only after the page LUT and the header's page count are written.
`abandonBuild()` removes the `.part`.

**The rename does not overwrite.** `FatFile::rename` opens the destination with
`O_CREAT | O_EXCL | O_WRONLY`
([SdFat FatFile.cpp:973](../../.pio/libdeps/default/SdFat/src/FatLib/FatFile.cpp)), so a
rename onto an existing path fails on FAT. The swap MUST therefore be
`Storage.remove(filePath)` (if present) followed by the rename, exactly as
`Section::commitBuildFile` already does ([Section.cpp:651-657](../../lib/Epub/Epub/Section.cpp)).
The host stub uses POSIX `::rename`, which overwrites silently, so the stub MUST be made to
refuse an existing destination or no test can catch this (Principle V).

**Rationale**: `Storage.rename()` exists ([HalStorage.h:60](../../lib/hal/HalStorage.h))
and `Section::binTmpPath()` sets the precedent
([Section.h:74-75](../../lib/Epub/Epub/Section.h)). Without it, an abandoned prefetch
leaves a header-only file at the real cache path; `loadSectionFile()`'s extent check does
reject it (`lutOffset >= HEADER_SIZE` fails when the placeholder 0 is still there —
[Fb2Section.cpp:124-136](../../lib/Fb2/Fb2/Fb2Section.cpp)), so this is a correctness
nicety rather than a fix. It is still worth the three lines: relying on a hostile-input
validation path for ordinary operation is the kind of thing that stops being true when
someone later changes the check.

**Note**: nothing in the tree enumerates `sections/`, so a stray `.part` is invisible to
every other code path; `ClearCacheActivity` removes the whole book directory
([ClearCacheActivity.cpp:119](../../src/activities/settings/ClearCacheActivity.cpp)).

---

## D7 — No cache version bump

**Decision**: `FB2_SECTION_FILE_VERSION` stays at 5
([Fb2Section.cpp:23](../../lib/Fb2/Fb2/Fb2Section.cpp)); `docs/file-formats.md` is not
edited.

**Rationale**: a sliced build writes the same header, the same serialized pages in the same
order, and the same LUT as the one-shot build. The equivalence test in
[contracts/build-api.md](contracts/build-api.md) §T1 is what proves this and must be
written before the slicing lands, not after.

---

## Measurement tasks

These MUST complete before the corresponding constants are committed. Each names the
device, the book and the reading, so the number that lands is a measurement and not a
round number with a story attached (Constitution IV).

### R1 — Slice byte budget

**Question**: how many bytes may be fed to expat in one slice while keeping the slice
shorter than one page render?

**Procedure**: on the Xteink X4 (ESP32-C3, `default` env, LOG_LEVEL=2) with the reference
1.9 MB / 66-chapter FB2 from issue #4, instrument `parseSome()` with `millis()` either side
and log the elapsed time per slice for candidate budgets across a scan-only region (early
slices of a late chapter) and a page-emitting region. Choose the largest budget whose
worst observed slice stays under the measured duration of a single in-chapter page render
on the same device.

**Blocks**: the `byteBudget` constant.

### R2 — Slice page budget

**Question**: how many pages may be laid out in one slice?

**Procedure**: as R1, over slices that emit pages. EPUB's `BACKGROUND_BUILD_PAGES_PER_TICK`
is 2 ([EpubReaderActivity.h:109](../../src/activities/reader/EpubReaderActivity.h)) and is
the starting hypothesis, **not** the answer: FB2 pages are plain text and may lay out
faster than EPUB's styled pages. Confirm or correct against the same page-render ceiling.

**Blocks**: the `pageBudget` constant.

### R3 — Idle settle interval

**Question**: how long after the last completed render may prefetch start, without
competing with a reader who is still turning pages?

**Procedure**: instrument inter-page-turn intervals for a normal reading pace and for
button-held skimming on device. The interval must exceed the skimming interval so that a
skimming reader never starts a prefetch that will immediately be stood down. EPUB's idle
prewarm uses 400 ms for a different job (rendering one page ahead into the font cache,
[EpubReaderActivity.cpp:352](../../src/activities/reader/EpubReaderActivity.cpp)) — a
reference point, not a default to copy.

**Blocks**: the settle-interval constant.

### R4 — Prefetch live-set size, and whether the existing heap gate suffices

**Question**: how much heap does an in-flight FB2 build hold, and are the reader's existing
32 KB free / 16 KB largest-block thresholds
([EpubReaderActivity.h:110-111](../../src/activities/reader/EpubReaderActivity.h)) the
right gate for it?

**Procedure**: log `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()` immediately before
`startBuild()`, at peak during a build, and after `abandonBuild()`, for the largest chapter
of the reference book. Components to attribute: the `Fb2SectionParser` object (including
its 201-byte `partWordBuffer` and `blockStyleStack`), the expat parser instance and its
buffer, the open `HalFile`, one in-flight `ParsedText`, one in-flight `Page`, and the page
LUT `std::vector<uint32_t>`.

**Blocks**: FR-012's gate values; also decides the `.reserve()` size for the LUT (seed it
from the observed page counts of the reference book's chapters, not from a guess).

**Note**: the expected result is that the gate is already adequate, because an on-demand
build holds the same objects today. The measurement exists to confirm that, not to assume
it.

### R5 — Boundary-crossing latency, before and after

**Question**: does the feature deliver SC-001?

**Procedure**: on the reference book with `.crosspoint/` cleared, read forward at a normal
pace across at least ten first-time chapter boundaries, logging the elapsed time from the
page-turn input to the first page being displayed. Compare against the same run with
prefetch disabled at compile time. Also record in-chapter page-turn latency in both runs
for SC-003.

**Blocks**: the completion claim. Nothing may be reported as "instant" without this.

---

## Simulator session, 2026-09-21 — what it settled and what it did not

Run on the **reference book itself**: `Марсианские хроники. Полное издание.fb2`,
1,915,806 bytes, which the reader parsed as **66 chapters** — the exact book and
chapter count issue #4 measured. Logs quoted below are from
`crosspoint-sim-feature-fb2-chapter-prefetch-simulator.log`.

### Settled (behavioural — the simulator is authoritative for these)

- **A prepared boundary does no work.** `[FBR] Loading section 2` is followed in the
  *same millisecond* by `[FBS] Loaded section: 21 pages`, with no
  `Cache not found, building...` and therefore no indexing popup. Across a session
  visiting twelve chapters, `Cache not found, building` appeared **once** — for
  chapter 0, which by definition cannot be prefetched. (SC-002, FR-006.)
- **Outrunning prefetch degrades to today's behaviour, never worse.** Holding page
  forward crossed five boundaries faster than prefetch could keep up; sections 4–8
  each logged `Cache not found, building...`, rendered correctly, lost no position
  and raised no error. Prefetch resumed by itself at chapter 9. (SC-006.)
- **Non-linear arrival retargets.** Jumping to chapter 33 from the chapter list
  built 33 on demand, then `Prefetching chapter 34` / `Prefetched chapter 34: 30 pages`.
  (Spec edge case "Non-linear arrival".)
- **Completed prefetch survives a kill.** With the process killed and relaunched,
  `Loading section 34` → `Loaded section: 30 pages` in the same millisecond: the
  chapter prepared before the kill was not rebuilt. (SC-005, and the half of FR-015
  this feature delivers.)
- **No `.part` ever leaks.** After every session above, `find` for `*.part` over the
  whole card returned nothing.

### A defect this session found

Ticking prefetch *after* `ReaderActivity::loop()` let it **restart a build that had
just been stood down**. Navigation from `loop()` is deferred, so the reader is still
the current activity when the tick runs; the log showed `Prefetching chapter 51`
twice, the second immediately before `Entering activity: EpubReaderMenu`, and the
`.part` survived under the menu. Fixed by ticking *before* the base loop, which
removes the ordering hazard without new state — all navigation is requested during
the base call. Re-tested with the byte budget temporarily cut to 128 (widening the
prefetch window to ~20 s so the race could be hit deliberately): one
`Prefetching chapter 51`, and no `.part` after the menu opened.

### NOT settled — still needs hardware

- **R1/R2/R3 remain open.** The simulator's loop rate is not the C3's, so no slice
  budget or settle interval can be chosen from it. One directional finding worth
  carrying to the device: at `PREFETCH_BYTES_PER_TICK = 4096`, preparing a *late*
  chapter took **10.8 s** in the simulator (chapter 35: `[19650]` → `[30439]`),
  because the parser rescans from byte 0. The byte budget, not the page budget,
  dominates a late chapter's preparation time — as D3 predicted. R1 should be read
  with that in mind: too small a byte budget makes late chapters effectively
  un-prefetchable at a normal reading pace.
- **R4 cannot be run here at all.** The simulator's `[MEM]` line is a hardcoded
  1 MB stub (`Free: 1048576, Total: 1048576, MaxAlloc: 1048576` on every tick), so
  every heap figure it reports is fiction.
- **SC-003 and SC-004 need the device.** Page-turn latency and minimum free heap
  cannot be compared meaningfully on a host allocator.
- **One stand-down path remains reasoned but unexercised**: a render-spec change
  arriving while a build is in flight *without* a sub-activity (the control centre
  turning the screen while the reader is stacked). Every other spec change routes
  through a sub-activity, which stands prefetch down first.

---

## Device session, 2026-09-21 — R1-R4 measured, and a design defect found

Xteink X4 (ESP32-C3, 268,352-byte heap), firmware built from this branch with a
temporary harness (`-DFB2_PREFETCH_BENCH` / `-DFB2_PREFETCH_MEASURE`, never
defined by a committed environment, removed after the readings). Book: the
largest FB2 on the card, 1,239,651 bytes / 23 chapters. **Not** issue #4's
1.9 MB / 66-chapter anthology — slice *duration* depends on bytes-per-slice and
per-buffer cost rather than file size, so R1/R2/R4 transfer, but any end-to-end
figure for preparing a *late* chapter scales with file size and would be ~1.5x
on the reference book.

### The ceiling

**A page render costs 996 ms mean / 1174 ms worst** on this panel. A slice holds
`RenderLock`, so its worst case is what a page turn can be made to wait for.

### R1 — byte budget (page budget unbounded, 53-page chapter)

| bytes | slices | worst | mean | total |
|-------|--------|-------|------|-------|
| 1024 | 191 | 95 ms | 13 ms | 2596 ms |
| 2048 | 96 | 114 ms | 26 ms | 2606 ms |
| **4096** | **48** | **200 ms** | **53 ms** | **2591 ms** |
| 8192 | 24 | 380 ms | 106 ms | 2596 ms |
| 16384 | 12 | 734 ms | 212 ms | 2590 ms |
| 32768 | 6 | 1458 ms | 427 ms | 2597 ms |

**Decision: 4096.** Total build time is flat across every budget, so a larger one
buys no throughput and only lengthens the worst block; 32768 exceeds a whole page
render. Smaller is safer per slice but multiplies slice count, and each slice is
one loop iteration.

### R2 — page budget (byte budget unbounded)

| pages | slices | worst | mean |
|-------|--------|-------|------|
| 1 | 44 | 322 ms | 58 ms |
| 2 | 24 | 389 ms | 106 ms |
| 4 | 13 | 463 ms | 196 ms |
| 8 | 7 | 651 ms | 364 ms |

**The byte budget is the tighter bound, not the page budget**: one page with bytes
unbounded is 322 ms against 4096 bytes' 200 ms. D3 predicted this and it is now
measured — copying EPUB's page-only pacing would have been the weaker bound.
The page budget is demoted to a backstop (4); at 4096 bytes a slice lays out
~1.1 pages, so it never binds in the measured data.

### R3 — idle settle interval

Twenty page turns, both paces:

- **Skim** (button held): floor **998 ms**, i.e. the panel's own render time.
- **Normal reading**: **17.6 s** minimum, up to 38.8 s.

**Decision: 1500 ms.** Above the skim floor, so a skimming reader never starts a
prefetch their next turn would interrupt; far below the reading floor, so a
reading one starts it almost at once.

### R4 — memory

| | |
|---|---|
| Heap at boot | free 154,492 / maxalloc 114,676 |
| After a page render | free 127,164 / maxalloc 90,100 (27,328 consumed) |
| Build live-set peak | **26,652 bytes** |
| Before / after an unbounded build | 127,132 / **127,132** — byte-identical |
| `maxalloc` during a build | unchanged at 90,100 |

**Gate: 56 KB free** = 26,652 (prefetch) + 27,328 (render), so prefetch cannot
start unless a page render also fits. Free at idle is ~150 KB, so it binds only
when it should. The largest-block gate stays at 16 KB: no large contiguous
allocation is needed. The identical before/after figures are independent
on-device confirmation of the expat-leak fix.

*(The one-slice unbounded run reports `peakheap=96`; that is a sampling artifact
— the harness samples after each slice, and there is only one. Ignore it.)*

### The defect: prefetch runs at 10 MHz

`HalPowerManager` drops the C3 to `LOW_POWER_FREQ = 10` MHz after
`IDLE_POWER_SAVING_MS = 3000` of no input ([HalPowerManager.h:29-33](../../lib/hal/HalPowerManager.h)).
Prefetch runs **only** when idle, so it ran almost entirely at 1/16 clock:

| | full clock | idle (10 MHz) |
|---|---|---|
| Worst slice | 162 ms | **3249 ms** |
| Throughput | 24.7 KB/s | **1.5 KB/s** |

A chapter was still unfinished after 110 s, projecting to ~14 minutes, and a
single slice blocked longer than three page renders. **The feature did not work
on the device it was written for**, and the R1 sweep had not caught it because
the bench ran at full clock.

Root cause: a byte budget bounds *bytes*, and bytes-per-millisecond moves 16x
depending on when the slice happens to run.

**Fix**: hold `HalPowerManager::Lock` for the lifetime of a build. It already
exists for exactly this ("a task that needs full performance",
[HalPowerManager.h:48-50](../../lib/hal/HalPowerManager.h)), so it is an RAII
member acquired at `startBuild` and released on completion or stand-down.

Verified on device:

```
[93493] Restoring normal CPU frequency      <- the lock engaging
[93496] Prefetching chapter 10
[93509] worst slice: 12ms  (slice 1)
[97872] worst slice: 233ms (slice 67)
[100610] Prefetched chapter 10: 55 pages
[100611] Going to low-power mode            <- released, clock drops back
```

**55 pages in 7.1 s, worst slice 233 ms** — against never-finishing and 3249 ms.

**The cost, stated plainly**: the CPU now runs at 160 MHz for a few seconds per
prepared chapter while the reader is idle, which is battery the user did not ask
for. It is bounded (the lock is released the moment the build ends, and prefetch
prepares at most one chapter ahead), but it is a real charge on a reading device
and was not weighed in the spec. Chosen deliberately over the alternative of
skipping prefetch while downclocked, which would have left the feature inert for
any chapter shorter than ~32 pages.

### SC-003 / SC-004

- **SC-003 passes**: worst slice 233 ms against a 996 ms page render. A turn that
  lands inside a slice waits at most one increment, ~23% of a render, which
  FR-008 permits and e-ink refresh masks.
- **SC-004 passes**: free heap across 26 turns including a sustained skim stayed
  within 150,572-151,688 — a 1.1 KB band with no drift. Session low-water was
  68,764 bytes against a 268,352-byte heap.

