# Contract: Prefetch Scheduling Policy

When `Fb2ReaderActivity` may run a prefetch slice, when it must pause, and when it must
stand down. The build machinery itself is [build-api.md](build-api.md); this file is the
policy that drives it, and it is where FR-004 and FR-007 to FR-014 are made concrete.

The governing rule is **P1**: the reader always wins.

## P1 — The reader always wins

Prefetch is strictly subordinate. Any doubt resolves as "skip this tick". A skipped tick
costs a few milliseconds of prefetch progress; a tick that should have been skipped costs
the user a visible stutter, which is the thing this feature exists to remove.

## P2 — Where the tick lives

```cpp
void Fb2ReaderActivity::loop() {
  ReaderActivity::loop();
  prefetchTick();
}
```

The base loop consumes input and may request an update
([ReaderActivity.cpp:145-176](../../../src/activities/reader/ReaderActivity.cpp)). The
tick runs after it, and its own gates (P3) decide whether to do anything. No new virtual
is added to `ReaderActivity`: a seam with one implementation is not earned here.

## P3 — Gates: all must hold to start or continue a slice

| # | Gate | Requirement |
|---|------|-------------|
| G1 | `fb2` and `section` exist and the current chapter is loaded | FR-004 |
| G2 | `!RenderLock::peek()` | FR-009 |
| G3 | `millis() - lastRenderCompleteMs > settleInterval` | FR-004, value from [research R3](../research.md) |
| G4 | no overlay, popup, menu, sub-activity or pending navigation | FR-008 |
| G5 | `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()` above the gate | FR-012, values from [research R4](../research.md) |
| G6 | not `onCoverPage`, not at end of book | FR-003 |

G1–G6 are re-checked on every tick, not only at start. Failing any of them while a build
is in flight means **pause**, not stand down — the build keeps its context and resumes on
a later tick.

## P4 — Choosing the target

The target is always `currentSectionIndex + 1`. Exactly one chapter, forward only (FR-002,
and the spec's Assumptions).

Prefetch does **not** start when:

- `currentSectionIndex + 1 >= fb2->getSectionCount()` (last chapter, or a single-chapter
  book) — FR-003
- `prefetchDoneIndex == currentSectionIndex + 1` (already prepared this session)
- a cheap header check shows `sections/<N+1>.bin` already loads under the current spec
  — FR-003

The header check is the existing `loadSectionFile()` validation on a throwaway
`Fb2Section`; it reads `HEADER_SIZE` bytes and no pages
([Fb2Section.cpp:67-140](../../../lib/Fb2/Fb2/Fb2Section.cpp)). Do not skip it — without
it, every re-entry to a chapter re-prepares a chapter that is already on the card.

## P5 — The slice

```cpp
RenderLock lock;                       // held for the slice only
prefetch->buildSomeMore(pageBudget, byteBudget);
```

The lock is required, not defensive: `loop()` runs on the Arduino task while `render()`
runs on the render task ([ActivityManager.cpp:32-46](../../../src/activities/ActivityManager.cpp)),
and layout reads the renderer's font metrics. EPUB's background build takes the same lock
for the same reason ([EpubReaderActivity.cpp:393](../../../src/activities/reader/EpubReaderActivity.cpp)).

**One slice per `loop()` iteration.** Never loop slices inside a tick — that reintroduces
the unbounded block the feature exists to remove.

Because the lock is held for the slice, slice duration is a direct tax on render latency.
That is what makes [research R1/R2](../research.md) blocking work rather than tuning.

## P6 — Stand-down

`abandonBuild()` then `prefetch.reset()`, on any of:

| Trigger | Why | Requirement |
|---------|-----|-------------|
| `currentSectionIndex` changed | the target is no longer *N+1* | FR-013 |
| the render spec differs from `prefetchSpec` | in-flight output is already stale | FR-016 |
| `buildSomeMore()` returned false | parse error or OOM | FR-018 |
| `onExit()` | activity teardown; member handles close here | FR-013 |
| the reader entered the chapter list, a percent jump, the menu, or any sub-activity | the reader's intent has moved | FR-008 |

On stand-down, `prefetchDoneIndex` is **not** set — the chapter was not prepared.

## P7 — Handing over: there is no handover

When the reader crosses the boundary, `Fb2ReaderActivity` does what it does today:
`section.reset()`, then the next `renderBook()` constructs a fresh `Fb2Section` for the new
index and calls `loadSectionFile()`
([Fb2ReaderActivity.cpp:408-425](../../../src/activities/reader/Fb2ReaderActivity.cpp)).
A completed prefetch means that call succeeds instead of falling through to
`createSectionFile()`.

If prefetch was still in flight for the chapter just entered, it is stood down by P6 and
the reader builds the chapter itself, exactly as today. This is the "reader outran
prefetch" case (SC-006): correct, no popup mismatch, no lost position, just no speed-up.

## P8 — Side effects

Prefetch writes `sections/<N+1>.bin` and its `.part`, and **deletes a stale
`sections/<N+1>.bin`**: the P4 header probe calls `loadSectionFile()`, which calls
`clearCache()` on a version or render-spec mismatch
([Fb2Section.cpp:83, 99, 113, 131](../../../lib/Fb2/Fb2/Fb2Section.cpp)). That delete is
intended — the file is unusable under the current spec and would be discarded at the
crossing anyway — but it is named here so it is not a surprise in a diff.

Those three paths are the whole side-effect surface. Prefetch MUST NOT touch
`progress.bin`, the recent-books store, `SETTINGS`, `APP_STATE`, the framebuffer, or any
displayed state (FR-019). It MUST NOT show the indexing popup — that popup means *the
user is waiting*, and during prefetch nobody is. `startBuild()` is therefore called with
no `BuildPopupFn`.

---

## §T — Test obligations

Activity-level scheduling is not reachable from the host suites (it depends on
`RenderLock`, `millis()` and `ESP.getFreeHeap()`), so these are verified in the simulator
and on device per [quickstart.md](../quickstart.md). What *is* host-testable is extracted
into a free function and tested:

| # | Test | Proves | Mutation it MUST catch |
|---|------|--------|------------------------|
| **TP1** | `shouldPrefetch(currentIndex, sectionCount, onCover, atEnd, doneIndex)` returns the target index or "none" | P4 | Off-by-one making the target `currentIndex` (prefetching the chapter being read) or running on the last chapter |
| **TP2** | The same function returns "none" when `doneIndex == currentIndex + 1` | P4 | Dropping the already-prepared check → every chapter re-entry re-prepares |

Put the pure predicate next to the existing FB2 reader helpers
(`src/activities/reader/Fb2ReaderMath.{h,cpp}`, already compiled and covered by the `xtc_fb2_readers` suite) so it
is host-reachable without stubbing the activity. Everything else in this contract is
verified interactively.
