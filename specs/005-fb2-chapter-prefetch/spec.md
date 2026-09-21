# Feature Specification: FB2 Next-Chapter Prefetch

**Feature Branch**: `feature/fb2-chapter-prefetch`

**Created**: 2026-09-21

**Status**: Draft

**Input**: GitHub issue [BlindBat/crosspoint-reader#4](https://github.com/BlindBat/crosspoint-reader/issues/4) — "feat: prefetch the next chapter's page cache in the background", the earliest open issue on the fork (opened 2026-09-20T11:16:32Z). User description: "Analyse all the open issues from the github and specify the earliest of them as a new feature."

## Summary

Reading an FB2 book forward stalls at every chapter boundary the first time it is
crossed, because the next chapter's pages are laid out on demand while the reader
waits. Once laid out, the same chapter opens effectively instantly. This feature
removes the stall by having the reader lay out the *next* chapter ahead of time,
while it is otherwise idle, so the reader never waits at a boundary it was walking
towards.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Crossing into the next chapter is instant (Priority: P1)

A reader is part-way through a chapter of a multi-chapter FB2 book. They keep
turning pages, reach the last page of the chapter, and turn once more. The next
chapter's first page appears as fast as an ordinary page turn — no progress popup,
no multi-second pause.

**Why this priority**: This is the entire complaint in issue #4 and the only
user-visible symptom. Everything else in this spec exists to make this safe.

**Independent Test**: Open a multi-chapter FB2 book with no cache present. Read to
the end of a chapter at a normal reading pace, then turn the page. The chapter
boundary turn completes in the same visual time as an in-chapter turn, and the
"Indexing" popup does not appear.

**Acceptance Scenarios**:

1. **Given** a chapter whose pages are laid out and a following chapter that has never been read, **When** the reader dwells in the current chapter long enough for the next one to be prepared, **Then** turning past the last page of the current chapter shows the next chapter's first page without an indexing popup and without a perceptible pause.
2. **Given** the next chapter was already prepared on a previous session, **When** the reader crosses the boundary, **Then** the behaviour is identical (the work is not redone).
3. **Given** the reader is on the last chapter of the book, **When** they reach its end, **Then** no preparation is attempted and the end-of-book screen behaves exactly as today.
4. **Given** the reader turns backwards across a chapter boundary into a chapter already read this session, **When** the previous chapter opens, **Then** it opens from its existing cache as it does today (backward crossing is not a new cost and needs no prefetch).

---

### User Story 2 - Preparation never gets in the reader's way (Priority: P1)

The reader is never made worse off by the preparation. Page turns, menus, chapter
selection, orientation changes and the status bar stay as responsive as they are
today, and a device short on memory silently does less preparation rather than
failing to render.

**Why this priority**: Equal-first with US1. On a single-core ~380 KB device, a
prefetch that competes for the CPU, the framebuffer, the storage mutex or the heap
would trade a 4-second stall at boundaries for stutter everywhere — a net loss. The
feature is only worth shipping if this holds.

**Independent Test**: With preparation active, measure page-turn latency and input
responsiveness inside a chapter and compare against the same book with preparation
disabled; and force a low-memory condition (large chapter, cover image loaded) and
confirm the reader still renders and turns pages.

**Acceptance Scenarios**:

1. **Given** preparation of the next chapter is in progress, **When** the reader turns a page, **Then** the page turn is served immediately and preparation yields to it.
2. **Given** preparation of the next chapter is in progress, **When** the reader opens a menu, the chapter list, or changes orientation, **Then** those actions behave exactly as they do today and any in-flight preparation is stood down.
3. **Given** free memory has fallen below the level at which the reader already pauses its own background layout work, **When** a preparation tick would run, **Then** preparation pauses and resumes only once memory recovers; the current chapter's rendering is unaffected.
4. **Given** preparation is running, **When** the reader's own page needs to be read from storage, **Then** the reader's read is not blocked waiting on preparation for longer than a single preparation step.
5. **Given** preparation would run, **When** the device is on the sleep/wake path or the screen is being refreshed, **Then** no preparation work starts.

---

### User Story 3 - Preparation work is never wasted (Priority: P2)

Whatever was prepared survives. If the reader closes the book, the device sleeps, or
the user jumps somewhere else mid-preparation, the pages already laid out are not
thrown away and are not laid out a second time.

**Why this priority**: Without it, an interrupted preparation is pure cost — battery
and storage writes spent for nothing — and a reader who leaves and returns pays the
original stall anyway. It is P2 because US1 still delivers value without it.

**Independent Test**: Begin reading a chapter, wait until preparation of the next one
has started but not finished, exit the book, reopen it, and cross the boundary.
Confirm the crossing does not redo the work already done before the exit.

**Acceptance Scenarios**:

1. **Given** preparation of the next chapter is part-finished, **When** the reader exits the book or the device sleeps, **Then** the part-finished work is retained and the next crossing resumes from it rather than starting over.
2. **Given** a text setting, font, margin or orientation change invalidates laid-out pages, **When** the reader resumes, **Then** any prepared-but-now-stale chapter is discarded and re-prepared under the new settings, never shown with the old layout.
3. **Given** preparation was interrupted, **When** the book is reopened, **Then** the reader's own current chapter is always prioritised over resuming preparation.

---

### Edge Cases

- **Last chapter**: nothing follows; preparation must not be attempted and must not produce an error.
- **Single-chapter book**: a book whose whole body is one chapter (the parser found no real section boundaries) has no next chapter; preparation is a no-op.
- **Non-linear arrival**: the reader jumps to a chapter via the chapter list, a percentage jump, a bookmark or a resume-from-progress. The chapter arrived at may not be prefetchable in advance; the chapter *after* it becomes the new preparation target once the reader settles.
- **Rapid page turning / skimming**: a reader holding the page button crosses boundaries faster than preparation can keep up. The crossing must still work correctly, just without the speed-up, and repeated preparation attempts must not pile up.
- **Reader turns back**: the reader moves backwards away from the boundary. Preparation of the chapter ahead is still valid (they will likely return), and must not be restarted from scratch when they come forward again.
- **Storage full or unwritable**: preparation cannot persist its result. It must fail quietly, leave the reader's own state untouched, and not retry in a tight loop.
- **Corrupt or truncated prepared cache**: a prepared chapter file that fails validation must be rejected and rebuilt on demand, exactly as an on-demand cache file is today — never trusted, never a crash.
- **Settings changed while preparation is in flight**: the in-flight work targets a layout that is already stale; it must be stood down rather than persisted as valid.
- **Very large chapter**: the chapter ahead is big enough that preparation cannot finish before the reader arrives. The crossing must degrade to today's behaviour (partial benefit or none), never to something worse than today.

## Requirements *(mandatory)*

### Functional Requirements

**Behaviour**

- **FR-001**: While reading an FB2 book, the system MUST prepare the page layout of the chapter immediately following the one being read, so that crossing forward into it requires no layout work at crossing time.
- **FR-002**: The system MUST prepare at most one chapter ahead at a time. (Rationale: one chapter is what a forward crossing needs; each additional in-flight chapter is additional live memory on a device whose RAM is the binding constraint — Constitution II.)
- **FR-003**: The system MUST NOT prepare a chapter when the reader is on the last chapter of the book, when the book has only one chapter, or when the chapter ahead is already fully prepared and current.
- **FR-004**: Preparation MUST begin only after the chapter being read is itself ready to display and the reader has been idle — no input, no pending screen update — for a settle interval.
- **FR-005**: A prepared chapter MUST be indistinguishable, when opened, from a chapter that was laid out on demand: same page count, same page breaks, same progress percentage.
- **FR-006**: Crossing a chapter boundary MUST NOT show the indexing/building popup when the chapter ahead is fully prepared.

**Yielding and responsiveness**

- **FR-007**: Preparation MUST be performed in bounded increments, each short enough that the device continues to service input, rendering and the watchdog between increments. A single increment MUST NOT hold the CPU for longer than an ordinary page render.
- **FR-008**: Any reader-initiated action — page turn, chapter selection, menu, percentage jump, orientation change, screen refresh, sleep — MUST take precedence over preparation, and MUST NOT wait for more than one in-flight increment to finish.
- **FR-009**: Preparation MUST NOT run while a screen update is in progress.
- **FR-010**: Preparation MUST NOT introduce an additional concurrent execution context; it MUST run within the reader's existing per-iteration work, the way the EPUB reader's own background layout already does ([src/activities/reader/EpubReaderActivity.cpp:390-403](../../src/activities/reader/EpubReaderActivity.cpp)). (Rationale: the ESP32-C3 is single-core and the firmware has exactly one application task — [src/activities/ActivityManager.cpp:32-46](../../src/activities/ActivityManager.cpp); a second task would contend for the same core, the same heap and the recursive storage mutex, and Constitution "Platform Constraints" forbids reader-core code that depends on multi-threading.)
- **FR-011**: Preparation MUST NOT hold the storage lock across increments; each increment MUST release it before yielding.

**Memory**

- **FR-012**: Preparation MUST NOT start, and MUST pause if already running, when free memory or the largest allocatable block is below the thresholds the reader already uses to pause its own background layout — 32 KB free / 16 KB largest block ([src/activities/reader/EpubReaderActivity.h:110-111](../../src/activities/reader/EpubReaderActivity.h)). (Rationale per Constitution IV: these are the in-tree, already-validated gate values for exactly this kind of background layout work; reusing them avoids inventing an unmeasured number. Whether FB2 preparation needs a *higher* gate MUST be settled by an on-device heap measurement during planning, not asserted.)
- **FR-013**: Preparation MUST release all memory it holds when it is stood down, when the reader leaves the book, and when the reader navigates away from the chapter it was preparing for.
- **FR-014**: The reader's own rendering path MUST retain priority on memory: if preparation and rendering cannot both be satisfied, preparation is the one that gives way.

**Durability and correctness**

- **FR-015**: Partially completed preparation MUST be retained across exit, sleep and navigation, so the work is resumed rather than repeated.
- **FR-016**: Prepared output MUST be invalidated whenever an on-demand layout would be invalidated — any change to the render settings, viewport, orientation, margins or font that the existing cache keying already covers.
- **FR-017**: Prepared output MUST be validated before use with the same checks applied to an on-demand cache file; a file that fails validation MUST be rejected and rebuilt, never partially trusted (Constitution VI).
- **FR-018**: A failure during preparation (storage error, parse failure, memory exhaustion) MUST be logged and MUST leave the reader's visible state and its own chapter cache untouched; the boundary crossing then behaves exactly as it does today.
- **FR-019**: Preparation MUST NOT write the reader's progress, alter the recent-books entry, or have any other side effect on stored user state.

**Scope boundaries**

- **FR-020**: This feature applies to the FB2 reader only. EPUB, TXT and XTC readers MUST be unchanged by it.

### Key Entities

- **Chapter ahead**: the chapter immediately following the one being read, in reading order. The single target of preparation at any moment.
- **Prepared page layout**: the laid-out pages of the chapter ahead, persisted to the book's cache so the next open finds them, keyed on the same render settings as any on-demand layout.
- **Preparation state**: whether preparation is idle, in progress, paused for memory, complete, or stood down — and which chapter it belongs to, so it can be discarded when the reader moves elsewhere.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On the reference book from issue #4 (1.9 MB FB2, 66 chapters, Xteink X4 / ESP32-C3), reading forward at a normal pace, a first-time chapter boundary crossing completes in the same order of time as an in-chapter page turn — compared against the measured baseline of roughly 4 s for a 42-page chapter built on demand, versus 7 ms for one already cached ([issue #4 device log](https://github.com/BlindBat/crosspoint-reader/issues/4)).
- **SC-002**: No indexing popup appears at a prepared chapter boundary.
- **SC-003**: Page-turn latency *within* a chapter, measured with preparation active, is not measurably worse than with preparation absent, on the same book and device.
- **SC-004**: Free heap at any point during a reading session with preparation active stays at or above the level observed for the same session without it, minus the documented preparation budget; the reader never fails to render a page it could render today.
- **SC-005**: Exiting and reopening the book part-way through preparation does not repeat layout work already completed, measured by the time to cross the boundary after reopening.
- **SC-006**: A reader skimming quickly enough to outrun preparation still crosses every boundary correctly, with no error popup, no lost position and no repeated build of the same chapter.
- **SC-007**: No regression in EPUB, TXT or XTC reading: those formats' behaviour and timings are unchanged.

## Assumptions

- **Forward only.** Backward crossings already land in a chapter whose layout was cached when it was read, so they cost nothing to reopen and need no preparation ([src/activities/reader/Fb2ReaderActivity.cpp:324-329](../../src/activities/reader/Fb2ReaderActivity.cpp)).
- **One chapter ahead.** Reading order is linear and forward; preparing two or more ahead multiplies memory and storage cost for a case (the reader crossing two boundaries before the first prepared chapter is consumed) that a normal reading pace does not produce.
- **No user-facing setting.** Preparation is either safe enough to be always on or not worth shipping; a toggle would be configuration for a value that never changes, and would add a settings string, a persisted field and a cache-keying question for no reading benefit. If on-device measurement shows a battery cost worth exposing, that becomes a separate decision.
- **Reuse of the existing cooperative pattern.** The EPUB reader already lays out pages incrementally from its per-iteration work under a heap gate; this feature is assumed to follow the same shape rather than introduce a new concurrency mechanism. FB2's layout is one-shot today ([lib/Fb2/Fb2/Fb2Section.h:17-19](../../lib/Fb2/Fb2/Fb2Section.h)), so making it resumable is the substantive work, not the scheduling.
- **Cache location and keying are unchanged.** Prepared chapters land in the book's existing per-chapter cache under `.crosspoint/fb2_<hash>/sections/`, keyed on the existing render spec, so invalidation rules need no new concept.
- **The settle interval and increment size are unmeasured.** Both are timing/throughput parameters that depend on the device and the book. Per Constitution IV they MUST be chosen from an on-device measurement during planning; this spec deliberately does not name a number for either.

## Out of Scope

- EPUB, TXT and XTC readers (decision recorded for issue #4's open question: FB2 only).
- Preparing more than one chapter ahead, or preparing backwards.
- Prefetching for non-linear arrivals (chapter list, percentage jump, bookmark, resume).
- Reducing the cost of laying out a chapter — this feature moves the cost off the critical path, it does not make the work cheaper.
- The first-open full-book indexing pass (measured at ~32 s in issue #4); that is a one-time cost on a different path.
- Any change to how FB2 chapter metadata is stored (issue #8) or how body-level front matter is weighted (issue #9).
