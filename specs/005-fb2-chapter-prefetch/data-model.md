# Phase 1 Data Model: FB2 Next-Chapter Prefetch

Two state machines and one live set. No persisted structure changes — see
[research.md D7](research.md).

---

## 1. `Fb2Section::BuildContext` (new, heap, transient)

Held only while a build is in progress. Mirrors `Section::BuildContext`
([Section.h:38-55](../../lib/Epub/Epub/Section.h)) but carries far less, because FB2
sections are text-only: no CSS parser, no extracted-HTML paths, no anchor map.

| Field | Type | Meaning |
|-------|------|---------|
| `parser` | `std::unique_ptr<Fb2SectionParser>` | The live parse. Owns the `XML_Parser` and the open input `HalFile`. |
| `lut` | `std::vector<uint32_t>` | File offset of each laid-out page, in page order. Written after the last page. `.reserve()`d at `startBuild` (size from [research R4](research.md)). |
| `lutCtx` | `BuildLutContext` | The existing context struct the page-complete callback takes ([Fb2Section.h:44-48](../../lib/Fb2/Fb2/Fb2Section.h)). Promoted from a `createSectionFile` local so it outlives a slice. |

**Allocation**: `makeUniqueNoThrow<BuildContext>()`; null means the build does not start
(`LOG_ERR`, return false). Nothing in this feature uses bare `new`.

**Lifetime**: created by `startBuild()`, destroyed by `finalizeBuild()` (success) or
`abandonBuild()` (stand-down, error, destructor). `isBuilding()` is exactly
`static_cast<bool>(build_)`.

### `Fb2Section` build state

| State | `isBuilding()` | `isBuildComplete()` | On disk |
|-------|----------------|---------------------|---------|
| Idle | false | false | nothing, or a finished `<n>.bin` |
| Building | true | false | `<n>.bin.part`, header written, pages appended so far, no LUT |
| Complete | false | true | `<n>.bin` — header patched with page count + LUT offset, LUT written, renamed from `.part` |
| Abandoned | false | false | `.part` removed; any pre-existing `<n>.bin` untouched |

**Transitions**

```
        startBuild(spec)            buildSomeMore(budgets) × N
Idle ─────────────────────► Building ──────────────────────► Building
                               │                                │
                               │ parse reached end of section   │
                               ▼                                │
                           Complete ◄────────────────────────────┘
                               
Building ──abandonBuild()──► Idle        (stand-down, parse error, OOM, destructor)
```

**Invariants**

- `startBuild()` on an already-building section is a programming error; it returns false
  and logs, it does not reset.
- `buildSomeMore()` with no build running returns false.
- A page's bytes are appended to the `.part` the moment it is laid out (unchanged from
  today's `onPageComplete` — [Fb2Section.cpp:33-47](../../lib/Fb2/Fb2/Fb2Section.cpp)), so
  the build holds one page at a time, never the whole chapter.
- `pageCount` on the object is the pages laid out **by this build**. Unlike EPUB there is
  no partial-file watermark to reconcile with ([plan.md Spec Deviations](plan.md)), so
  `pageCount` has exactly one meaning.
- The destructor abandons any running build. It must not leave a `.part` behind.

---

## 2. `Fb2SectionParser` resumable-parse state (existing fields, new lifetime)

Every field listed in [Fb2SectionParser.h:26-70](../../lib/Fb2/Fb2/Fb2SectionParser.cpp)
is already a member and already survives between `XML_ParseBuffer` calls. Slicing changes
*nothing* about them. What changes is that three things stop being locals of
`parseAndBuildPages()`:

| Promoted to member | Was | Why |
|--------------------|-----|-----|
| `XML_Parser xmlParser` | local, `XML_ParserFree` at end of function | must survive between slices |
| `HalFile file` | local | must stay open and positioned between slices; auto-closes with the parser object (`DESTRUCTOR_CLOSES_FILE=1`) |
| `bool done` | loop local | the feed cursor's "input exhausted" flag |

Plus two new counters, reset at the start of each slice:

| Field | Type | Meaning |
|-------|------|---------|
| `slicePagesEmitted` | `uint16_t` | pages completed in this slice; compared against `pageBudget` |
| `sliceBytesFed` | `uint32_t` | bytes handed to `XML_ParseBuffer` in this slice; compared against `byteBudget` ([research D3](research.md)) |

### Parse state

| State | Meaning |
|-------|---------|
| Unstarted | `beginParse()` not yet called |
| Feeding | more input to read, target section not finished |
| Finished | `pastTargetSection`, or input exhausted, or `outOfMemory` |

`parseSome()` returns a status distinguishing **paused** (budget spent, more to do) from
**finished** from **failed**; the existing `parseAndBuildPages()` one-shot wrapper loops on
paused so its behaviour is bit-for-bit unchanged.

**Invariant that carries the whole design**: the sequence of pages emitted must not depend
on where slice boundaries fall. A slice may end mid-tag, mid-text-node or mid-word —
expat's own buffering and the existing `partWordBuffer` handle that, and
[contracts/build-api.md](contracts/build-api.md) §T2 is the test that proves it.

---

## 3. `Fb2ReaderActivity` prefetch state (new)

| Field | Type | Meaning |
|-------|------|---------|
| `prefetch` | `std::unique_ptr<Fb2Section>` | The chapter-ahead build. Null when no prefetch is in flight. |
| `prefetchIndex` | `int` | Which chapter `prefetch` is building. Meaningless when `prefetch` is null. |
| `prefetchSpec` | `ReaderRenderSpec` | The spec the in-flight build was started with, so a settings change can be detected and the build stood down (FR-016). |
| `prefetchDoneIndex` | `int` | The last chapter index prefetch completed, so a completed target is not re-attempted. `-1` = none. |
| `lastRenderCompleteMs` | `unsigned long` | Set when `renderBook()` finishes; the idle settle timer's origin (FR-004). |

### Prefetch state machine

```
                      ┌──────────────────────────────────────────┐
                      │                                          │
   Idle ──may start?──► Building ──slice──► Building ──done──► Satisfied
    ▲    (all gates)      │                                      │
    │                     │                                      │
    └──────stand down─────┴──────────────────────────────────────┘
       (reader moved, spec changed, error, onExit)
```

**May start** requires *all* of:

1. `section` exists and is loaded (the reader's own chapter is ready) — FR-004
2. `prefetch` is null and `prefetchDoneIndex != N+1`
3. `N+1 < fb2->getSectionCount()` — FR-003
4. `!onCoverPage` and not at end of book
5. `millis() - lastRenderCompleteMs > settleInterval` — FR-004, [research R3](research.md)
6. `!RenderLock::peek()` — FR-009
7. no overlay, popup or pending navigation is active — FR-008
8. free heap and largest block above the gate — FR-012, [research R4](research.md)
9. `sections/<N+1>.bin` does not already load under the current spec (cheap header check;
   avoids rebuilding a chapter the reader has already visited) — FR-003

**Must pause** (keep the build, skip this tick): gate 6, 7 or 8 fails.

**Must stand down** (`abandonBuild()`, `prefetch.reset()`):

| Trigger | Why |
|---------|-----|
| current chapter index changed | the target is no longer *N+1* — FR-013 |
| render spec changed (orientation, font, margins, line spacing, …) | in-flight output is already stale — FR-016 |
| `buildSomeMore()` returned false | parse error / OOM — FR-018 |
| `onExit()` | activity teardown — FR-013 |
| the reader opened the chapter list, a percent jump, or any sub-activity | FR-008 |

**Never**: prefetch writes progress, touches recents, or changes anything the reader
displays (FR-019). Its only side effects are `sections/<N+1>.bin{,.part}`.

---

## 4. Live set during a prefetch slice

What is held in RAM while a slice runs. Every item is already held by today's on-demand
build; prefetch changes *when*, not *what*.

| Item | Note |
|------|------|
| `Fb2Section` (prefetch) | small: two `std::string` paths, `pageCount`, output `HalFile` |
| `BuildContext` | `unique_ptr` + `lut` vector (4 bytes/page) |
| `Fb2SectionParser` | includes the 201-byte `partWordBuffer` and `blockStyleStack` (reserved at 4) |
| expat `XML_Parser` + its buffer | `XML_CONTEXT_BYTES=1024`, 1024-byte feed buffer |
| input `HalFile` | open on the `.fb2` |
| one in-flight `ParsedText` | the block being laid out |
| one in-flight `Page` | flushed to the `.part` as soon as it is complete |

**Not held**: the chapter's pages (streamed to disk), any framebuffer, any glyph data
beyond what `FontDecompressor` already caches for the reader's own rendering.

Sizes are attributed and totalled by [research R4](research.md). No figure is asserted
here (Constitution IV).
