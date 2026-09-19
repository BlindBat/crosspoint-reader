# Implementation Plan: Stub Cover Sleep Screen

**Branch**: `002-stub-cover-sleep-screen` | **Date**: 2026-09-19 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-stub-cover-sleep-screen/spec.md`

## Summary

Draw a framed title/author card when a cover sleep screen finds no usable cover art, and wrap
both texts over multiple lines instead of truncating them to one.

The approach is entirely reuse. `renderCoverSleepScreen()` already loads the book object it needs
for the cover; the title and author come off that same object, so no extra parse happens. The
wrapping and multi-line centring already exist as `UITheme::drawCenteredWrappedText()`
([src/components/UITheme.cpp:145-181](../../src/components/UITheme.cpp)), which wraps only what
overflows, clamps the line count to the bounds it is given, and vertically aligns the resulting
block. Feeding it two bounds rectangles that meet at a fixed divider makes the title grow upward
and the author downward, so the block stays balanced at any line count with no layout arithmetic
of our own.

Net change: one function body in `SleepActivity.cpp`, no new file, no new helper, no new test
target, no new setting, no new stored data.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), `-fno-exceptions`, no RTTI

**Primary Dependencies**: existing in-tree only — `GfxRenderer` (text metrics, wrapping, rect
drawing, screen inversion), `UITheme` (`drawCenteredWrappedText`), the four format readers
(`Epub`, `Fb2`, `Txt`, `Xtc`) already used by the cover path, `HalDisplay` (half refresh)

**Storage**: none. The card is drawn from metadata already in memory and is never persisted; no
cache file, no cache version, no setting

**Testing**: `bin/run-tests` (host gtest). The wrapping engine this feature leans on is already
covered by `test/gfx_renderer/GfxRendererTest.cpp` (behaviour) and
`test/gfx_renderer/GfxRendererAllocTest.cpp` (allocation ceiling). Pixel-level verification is the
simulator, per constitution gate 6

**Target Platform**: ESP32-C3 (`default`) and the four ESP32-S3 boards; the code path is
board-independent and compiles into every environment

**Project Type**: embedded firmware — single binary per MCU family, no client/server split

**Performance Goals**: exactly one screen update (half refresh), matching the other static sleep
screens; zero additional SD reads beyond the cover lookup that already happens

**Constraints**: ~380 KB RAM on C3; single 48 KB framebuffer; runs during sleep entry, where the
device is about to cut power to peripherals, so allocation and I/O must both stay flat

**Scale/Scope**: one screen, one function, four book formats

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design — unchanged.*

| Principle | Verdict | Evidence |
|---|---|---|
| I. A Focused Reading Device | PASS | Improves the core reading experience directly: the cover sleep screen is currently inert for every book without cover art. No new subsystem, no connectivity, no new theme. |
| II. Memory Is the Design Constraint | PASS | Two `std::string` copies of title/author (short, sleep-entry only, freed at scope exit) plus the `std::vector<std::string>` that `wrappedText` already returns for every list row in the UI — it reserves `min(maxLines, 8)` up front, so no doubling chain. No new buffer, no framebuffer copy (the card is drawn straight into the existing framebuffer, no grayscale snapshot). Flash cost is a few hundred bytes of new code. |
| III. Portability Behind the HAL | PASS | Touches only `src/activities/`; reaches hardware exclusively through `GfxRenderer` and `HalDisplay`. No new HAL surface, no board conditionals. |
| IV. Evidence Over Claims | PASS | Every claim here cites the file and lines it rests on. The "one refresh" and "no extra parse" claims are structural (one `displayBuffer` call; metadata read from the already-loaded book object) and are verified on screen in the simulator, not asserted from host results. |
| V. Tests Prove Behavior | PASS (with note) | The feature adds no host-reachable logic: the wrapping and centring are existing, already-covered code, and what remains in `SleepActivity` is render calls plus one three-way branch that is not host-compilable. Extracting that branch into `sleepimage::` for a host test was considered and rejected — it reduces to `coverReadable ? Art : (title.empty() ? Fallback : Stub)`, and a test asserting a one-line ternary is the theatre this principle exists to forbid. Verification is gate 6 (simulator), with the recipe in [quickstart.md](quickstart.md). |
| VI. Untrusted Input Is Hostile | PASS | Title and author are attacker-shaped (they come out of an EPUB/FB2 container). They are never passed to a C string API as a `string_view`, never used to size an allocation, and their length is bounded by the wrap helper, which clamps lines to the bounds height and ellipsises the remainder. A hostile title costs at most three wrapped lines. |
| VII. Upstream-First Fork Hygiene | PASS | One logical change, one semantic commit, on a short-lived branch. No fork-only tooling in the diff; the change is upstream-shaped (it extends an existing upstream function rather than adding a fork subsystem) and would port as a small PR. |

### Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| No host test for the cover/stub/fall-back branch (tasks-template override of Principle V) | The branch lives in `SleepActivity`, which pulls the display, settings and storage singletons and does not host-compile; the wrapping it delegates to is already covered by `test/gfx_renderer/` | Extracting the branch to `sleepimage::` yields `coverReadable ? Art : (title.empty() ? Fallback : Stub)` — a test over a one-line ternary does not fail when the feature breaks, which Principle V itself defines as theatre. Verification is gate 6 (simulator), recipe in [quickstart.md](quickstart.md). |

This is the only entry; Principles I, II, III, IV, VI and VII pass without qualification.

## Project Structure

### Documentation (this feature)

```text
specs/002-stub-cover-sleep-screen/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output — simulator validation recipe
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

No `contracts/` directory. The feature exposes no interface to anything outside the firmware: no
new setting, no new persisted file or cache version, no new user-facing string needing
translation, no network or web-server surface. Its only contract is the rendered screen, which is
specified by FR-007 to FR-015 and validated visually.

### Source Code (repository root)

```text
src/activities/boot_sleep/
├── SleepActivity.h      # declares renderCoverStubSleepScreen(title, author)
└── SleepActivity.cpp    # renderCoverSleepScreen() fall-through + the card renderer
```

**Structure Decision**: a single activity, edited in place. The cover lookup and the card renderer
are two functions in the file that already owns every sleep screen; splitting either out would add
a file and a seam for no test and no reuse. The pure helpers that *did* earn extraction —
header validation and image placement — already live in `SleepImageUtils` beside it, and this
feature needs neither.

## Phase Outputs

- **Phase 0**: [research.md](research.md) — four decisions, no unresolved unknowns.
- **Phase 1**: [data-model.md](data-model.md), [quickstart.md](quickstart.md). No contracts (see
  above).
- **Phase 2**: `/speckit-tasks` — expected to be a handful of tasks; the shape is in research.md
  §Implementation shape.
