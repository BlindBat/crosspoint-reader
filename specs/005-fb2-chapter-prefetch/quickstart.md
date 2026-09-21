# Quickstart: Verifying FB2 Next-Chapter Prefetch

How to prove the feature works, in the order the evidence gets more expensive to obtain.
Host suites prove the layout is unchanged; the simulator proves the reader stays
responsive; only the device proves the stall is gone.

Nothing may be reported as done on host results alone. Constitution IV: on-device
behaviour is verified on the device or the official simulator, never assumed from host
results — and the macOS caveat in the Development Workflow section means a green local
run is not a merge signal by itself.

---

## 0. Prerequisites

```bash
# Host toolchain (macOS: point CC/CXX at Xcode clang first)
bin/run-tests --help

# Device build
pio run -e default
```

Test material:

- `test/fb2/` — existing fixtures, including `nested-sections.fb2` from feature 003.
- The reference book from [issue #4](https://github.com/BlindBat/crosspoint-reader/issues/4):
  a 1.9 MB FB2 with 66 chapters. Every timing claim in this feature is against this book
  on an Xteink X4. Substituting a different book invalidates the comparison.

---

## 1. Host: the layout did not change

The one result that matters most. If a sliced build differs from a one-shot build by a
single byte, stop — the cache is now a correctness hazard, not a speed-up.

```bash
bin/run-tests                      # plain
bin/run-tests --asan               # ASan + UBSan, detect_leaks=1
```

Expect green on `fb2_section_cache`, `fb2_section_parser`, `xtc_fb2_readers` and
`alloc_guards`. The obligations these must satisfy are listed in
[contracts/build-api.md §T](contracts/build-api.md) (T1–T9) and
[contracts/prefetch-policy.md §T](contracts/prefetch-policy.md) (TP1–TP2).

**Mutation check** — every new suite must be shown to fail against broken code before it
counts (Constitution V). The cheapest demonstration for the load-bearing one:

```cpp
// In Fb2SectionParser::parseSome(), at slice entry — T1 must go red:
nextWordContinues = false;
```

Build, run `fb2_section_cache`, confirm T1 fails, revert.

**Expected**: all suites green; each new suite demonstrably red under its named mutation.

---

## 2. Host: allocations did not grow

```bash
bin/run-tests --asan                # alloc_guards runs here
```

The guard covers the prefetch build path. A regression that turns the page LUT's
`reserve()` into a growth loop, or adds a per-slice allocation, shows up as a changed
allocation count without needing hardware. This is the constitution's accepted proxy for
memory-behaviour regressions — it is not a substitute for R4.

**Expected**: allocation counts for a sliced build equal those for a one-shot build, plus
the one `BuildContext`.

---

## 3. Simulator: the reader stays responsive

```
/run-simulator
```

(or `pio run -e simulator` with the per-developer `platformio.local.ini` config — the
skill handles the toolchain, the per-branch SD card and the version stamping.)

Copy the reference book to the simulator's SD card and **delete `.crosspoint/`** so no
chapter is pre-built.

| Check | What to look for | Requirement |
|-------|------------------|-------------|
| Read forward through a chapter boundary | first page of the next chapter appears with no "Indexing" popup | FR-006, SC-002 |
| Hold the page button to skim across several boundaries | every boundary crosses correctly; no error popup, no lost position; no speed-up expected | SC-006 |
| Open the chapter list mid-prefetch | list opens immediately; prefetch stands down | FR-008, P6 |
| Change font size / orientation mid-prefetch | re-pagination behaves as today; no stale page appears | FR-016 |
| Jump by percentage | lands correctly; prefetch retargets after the reader settles | FR-008 |
| Read to the last chapter | end-of-book screen unchanged; no prefetch attempted | FR-003 |
| Open a single-chapter FB2 | no prefetch, no error | Edge cases |

**Expected**: behaviour identical to today except that prepared boundaries do not show the
popup. The simulator will not show the timing win convincingly — its storage is not an SD
card.

---

## 4. Device: the measurements ([research.md](research.md) R1–R5)

Xteink X4 (ESP32-C3), `pio run -e default -t upload`, LOG_LEVEL=2, monitored with:

```bash
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101   # macOS
```

Run R1–R4 **first** — their results choose the constants. R5 is the acceptance run and
must be repeated after the constants are fixed.

### R5 acceptance run

1. Delete `.crosspoint/` on the card. Copy the reference book.
2. Open it and read forward at a normal pace across at least ten first-time chapter
   boundaries.
3. From the log, record for each boundary: page-turn input → first page displayed.
4. Repeat with prefetch disabled at compile time, same card state, same boundaries.

| Metric | Baseline (issue #4) | Target |
|--------|---------------------|--------|
| First-time boundary crossing | ~4 s for a 42-page chapter | same order as an in-chapter page turn |
| Cached chapter load | 7 ms | unchanged |
| In-chapter page turn | measure both runs | no measurable regression — SC-003 |
| Free heap, min over session | measure both runs | no worse than the no-prefetch run beyond the R4 budget — SC-004 |

Log lines to watch: `[FBR] Loading section N`, `[FBS] Loaded section: N pages`, and the
`[MEM]` heap traces.

**Expected**: prepared boundaries drop from seconds to milliseconds; in-chapter turns and
minimum free heap are indistinguishable between the two runs.

### Interrupted-prefetch check (FR-015, partial — see [plan.md](plan.md) Spec Deviations)

Start a chapter, wait for prefetch to begin but not finish, exit the book, reopen, cross
the boundary.

**Expected**: the crossing behaves exactly as today (the chapter is built on demand), and
no `.part` file is left on the card. A *completed* prefetch, by contrast, survives and the
crossing is instant. The in-flight case is a known, documented gap, not a defect.

---

## 5. Merge gates

Per the constitution, in order:

```bash
./bin/clang-format-fix -c
bin/run-tests
bin/run-tests --asan
pio run -e default
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

Then read the CI run. On macOS, gates 2, 3 and 5 do not predict CI — libc++ resolves
headers libstdc++ does not, and `pio check` analyses nothing while still reporting PASSED.

This feature touches rendering-adjacent code and an activity, so the simulator pass
(step 3) is required, and it makes timing and heap claims, so the device pass (step 4) is
required too.
