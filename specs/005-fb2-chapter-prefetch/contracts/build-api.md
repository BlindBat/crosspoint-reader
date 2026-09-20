# Contract: `Fb2Section` Incremental Build

Normative rules for the resumable FB2 layout API and the parse that backs it. The single
rule everything else serves is **B1**; if it breaks, prefetched chapters render
differently from on-demand ones and the cache becomes a correctness hazard rather than a
speed-up.

Shape deliberately mirrors `Section`'s EPUB equivalent
([Section.h:91-99](../../../lib/Epub/Epub/Section.h)) so the two readers stay legible
side by side.

## B1 — Slice-independence (load-bearing)

For a given chapter and `ReaderRenderSpec`, the bytes written to `sections/<n>.bin` MUST
be **identical** regardless of how the build was sliced: one shot, one page at a time, one
byte-budget at a time, or any mixture. Identical means the header, the serialized page
stream, the page count and the LUT — byte for byte.

Corollary: `FB2_SECTION_FILE_VERSION` does not change
([research D7](../research.md)), because a sliced build is not a new format.

## B2 — API surface

```cpp
bool startBuild(const ReaderRenderSpec& spec);   // open .part, write header, begin parse
bool buildSomeMore(int pageBudget, uint32_t byteBudget);  // one slice
bool isBuilding() const;                         // a BuildContext exists
bool isBuildComplete() const;                    // the last build finished successfully
void abandonBuild();                             // drop context, remove .part
```

- `startBuild()` returns false and logs on: no `BuildContext` allocation, no `.part` file,
  a build already running. It MUST NOT reset a running build.
- `buildSomeMore()` returns false and logs on parse error or OOM, and MUST
  `abandonBuild()` itself before returning, so a caller that stops looking at the object
  leaves nothing behind.
- `buildSomeMore()` with no build running returns false without logging an error.
- Reaching the end of the target section inside `buildSomeMore()` MUST finalize (write
  LUT, patch header, rename `.part` over `.bin`) and set `isBuildComplete()` in the same
  call. There is no separate `finalize()` for callers to forget.
- `createSectionFile(spec, popupFn)` MUST become exactly
  `startBuild` + `buildSomeMore(unbounded)` until complete, so existing callers and the
  `fb2_section_cache` suite see no behaviour change.

A budget `<= 0` / `0` means unbounded for that dimension.

## B3 — Budgets

A slice ends when **either** budget is spent, whichever comes first:

- `pageBudget` — pages emitted by the page-complete callback during this slice.
- `byteBudget` — bytes handed to `XML_ParseBuffer` during this slice.

Both counters reset at slice entry. The byte budget is not optional: a slice building a
late chapter emits no pages at all while it scans to the section
([research D3](../research.md)), so a page-only budget does not bound slice duration.

Budgets are checked **between** `XML_ParseBuffer` calls, never inside a handler. A slice
therefore always ends on a buffer boundary and may overrun its page budget by however many
pages one 1024-byte buffer completes.

## B4 — Atomicity

- A build writes to `binTmpPath()` = `<filePath> + ".part"`, never to `filePath`.
- `filePath` is replaced by a single `Storage.rename()` **after** the page stream, the LUT
  and the patched header are all written.
- A pre-existing `sections/<n>.bin` stays readable and valid for the entire build. A build
  that is abandoned leaves it exactly as it was.
- `abandonBuild()` removes the `.part`. So does the destructor.
- A stale `.part` found at `startBuild()` is overwritten, not appended to.

## B5 — Parser resumability

- `Fb2SectionParser` gains `beginParse()` / `parseSome(pageBudget, byteBudget)` /
  `finishParse()`; `parseAndBuildPages()` remains as a wrapper that loops `parseSome`
  until it stops returning *paused*.
- `parseSome()` MUST distinguish three outcomes: **paused** (budget spent, more input),
  **finished** (target section closed, or input exhausted), **failed**.
- A slice boundary may fall anywhere in the input — mid-tag, mid-attribute,
  mid-text-node, mid-word. No parser field may be reset between slices except the two
  slice counters.
- `finishParse()` performs the existing tail flush (`makePages()`, emit the last partial
  page, release `currentTextBlock` — [Fb2SectionParser.cpp:501-510](../../../lib/Fb2/Fb2/Fb2SectionParser.cpp))
  and frees the `XML_Parser`. It MUST be idempotent.
- `Hyphenator::setPreferredLanguage()` is called once at `beginParse()`, as today
  ([Fb2Section.cpp:187](../../../lib/Fb2/Fb2/Fb2Section.cpp)). It is global state; both the
  reader's own build and a prefetch of the same book set the same value, so no ordering
  hazard exists. A future prefetch across *different* books would break this — out of
  scope, and the reason prefetch is confined to the open book.

## B6 — Memory

- `BuildContext` via `makeUniqueNoThrow`; null → `LOG_ERR` + return false. No bare `new`.
- `lut.reserve()` before the first page, sized per [research R4](../research.md). No
  `push_back` loop without a prior reserve.
- The build holds at most one `Page` and one `ParsedText` at a time; pages are serialized
  to the `.part` on completion as they already are
  ([Fb2Section.cpp:33-47](../../../lib/Fb2/Fb2/Fb2Section.cpp)).
- No buffer over 256 bytes on the stack.

## B7 — Storage discipline

- Every SD access goes through `HalStorage` / `HalFile`.
- A slice MUST NOT hold a `HalFile` operation across its own return; the input file stays
  *open* between slices (that is the point), but no lock is held while paused. `HalFile`
  takes `storageMutex` per call, so a paused build holds nothing.
- No explicit `close()` on local `HalFile`s (`DESTRUCTOR_CLOSES_FILE=1`). The parser's
  member input file and the section's member output file close at their objects'
  destruction, or explicitly before the rename in B4.

---

## §T — Test obligations

Each suite below MUST demonstrate at least one production mutation it catches
(Constitution V). The named mutation is the minimum; it must be shown failing against the
mutated code before the suite is accepted.

| # | Test | Proves | Mutation it MUST catch |
|---|------|--------|------------------------|
| **T1** | Build a chapter one-shot; build the same chapter with `pageBudget=1`; compare the two `sections/<n>.bin` files byte for byte | B1 | Reset any parser field at slice entry (e.g. `nextWordContinues = false`) → files diverge |
| **T2** | Build the same chapter with `byteBudget` set to 1 buffer, then 3, then 7, then unbounded; all four files identical | B1, B3, B5 | Check budgets *inside* `characterData` instead of between `XML_ParseBuffer` calls → a slice ends mid-node and text is dropped or duplicated |
| **T3** | Build a chapter whose content spans a slice boundary mid-word; assert the word is intact in the rendered page text | B5 | Clear `partWordBuffer`/`partWordBufferIndex` at slice entry → word splits across pages |
| **T4** | `startBuild`, one slice, `abandonBuild`; assert no `.part` remains and a pre-existing `<n>.bin` is byte-unchanged | B4 | Write in place instead of to `.part` → the pre-existing file is clobbered |
| **T5** | `startBuild`, some slices, destroy the `Fb2Section`; assert no `.part` remains | B4 | Omit `abandonBuild()` from the destructor → leaked `.part` |
| **T6** | `buildSomeMore` on a section with no build running returns false and does not crash | B2 | Dereference `build_` without a null check |
| **T7** | Drive a build to completion through slices; assert `isBuildComplete()`, then `loadSectionFile()` on a fresh `Fb2Section` succeeds with the same page count and the same page text as the one-shot build | B1, B2, B4 | Rename before writing the LUT → `loadSectionFile`'s extent check rejects the file |
| **T8** | Malformed FB2 from the existing corpus, built through slices; assert bounded failure, no `.part` left, sanitizer-clean | B2, B4, Principle VI | Return *finished* instead of *failed* on `XML_STATUS_ERROR` → a truncated file is renamed into place |
| **T9** | `createSectionFile()` output is byte-identical to the pre-change implementation for every fixture in `test/fb2/` | B2 | Any behavioural drift in the refactor |

**Where**: T1–T7 and T9 in `test/fb2_section_cache/`; T2, T3 and T8 need parser-level
fixtures and belong in `test/fb2_section_parser/`. Both suites already exist and already
link the machinery.

**Allocation proxy**: add the prefetch build path to `test/alloc_guards/` so a regression
that turns the LUT's reserve into a growth loop, or adds a per-slice allocation, is caught
without a device (Constitution IV's accepted proxy).
