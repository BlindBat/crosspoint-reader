<!--
Sync Impact Report
==================
Version change: 1.0.0 → 2.0.0 (MAJOR: Principle VII's "master/develop MUST remain clean
upstream mirrors" rule is redefined; fork `master` becomes the integration branch)
Modified principles:
  - VII. Upstream-First Fork Hygiene (title unchanged): branch model redefined. Fork
    `master` carries fork work; no direct commits on `master`/`develop`; short-lived
    feature/fix branches land by fast-forward or merge and are deleted; upstream syncs
    merge `origin/develop` through a sync branch; upstream PR branches are cut from
    `origin/develop`; tests/QA tooling stay out of upstream PR branches. Rationale extended.
  - V. Tests Prove Behavior (title unchanged): merge gate is now "merge into fork `master`";
    rationale no longer cites the retired `qa/autotests` branch.
Modified sections:
  - Development Workflow & Quality Gates: gates apply to every merge into fork `master`
    (feature, fix, upstream sync); gate tooling lives on `master`, never in upstream PR
    branches.
Added sections: none
Removed sections: none
Templates reviewed: none edited (plan/spec/tasks templates resolve this file at runtime
  and carry no branch-model text)
Runtime guidance reviewed:
  - AGENTS.md ✅ ("PR comparisons target develop" governs upstream-bound work; consistent
    with PR branches cut from `origin/develop`)
  - .specify/scripts/bash/speckit-commit.sh ⚠ comments and refusal message still call
    master/develop "clean upstream mirrors" (guard behavior itself stays correct)
  - .specify/extensions/speckit-git-commit/SKILL.md ⚠ same "mirrors" wording
Deferred TODOs: none in this file (the two wording follow-ups above live outside it)
-->

# CrossPoint Reader Constitution

Fork: BlindBat/crosspoint-reader — tracks crosspoint-reader/crosspoint-reader (upstream).
This constitution governs work done in this fork. It distills upstream's published vision
(SCOPE.md), engineering rules (AGENTS.md), and community norms, plus the fork's own
quality program, into gates that specs, plans, and implementations MUST pass.

## Core Principles

### I. A Focused Reading Device

Every change MUST directly improve the core reading experience or the firmware's
long-term maintainability. The scope test is upstream's own delineator: (a) does the
stock firmware already do this well? (b) does another popular fork already solve it
well outside the core reading experience? (c) does it improve reading or
maintainability? Failing (c) means out of scope. Interactive apps, authoring tools,
active connectivity (RSS/news/browsers), and PDF rendering are out of scope, period.
New themes and new external network connectors are closed areas until upstream lifts
the notice. When scope is uncertain, a Discussion MUST be opened before code is
written.

Rationale: upstream's SCOPE.md — a dedicated e-reader should do one thing
exceptionally well; every accepted change must make that easier, not harder.

### II. Memory Is the Design Constraint

The ESP32-C3's ~380 KB usable RAM is a hard ceiling and sets the bar for every
target. Concretely: every new heap allocation (`new`, `malloc`, `std::vector`
growth) MUST be justified or replaced with a stack/static/flash alternative; bare
`new` MUST NOT be used for fallible allocations (`-fno-exceptions` makes it an
abort) — use `new (std::nothrow)` or `makeUniqueNoThrow<T>()`; `.reserve(N)` MUST
precede `push_back` loops; large constant data MUST be `constexpr`/`static const`
so it stays in flash; `std::string`/Arduino `String` MUST NOT appear in hot paths;
local buffers over 256 bytes MUST NOT live on the stack; SD writes MUST be
guarded against redundancy and debounced. Flash footprint is a shared budget:
binary growth MUST be justified the same way RAM growth is.

Rationale: memory pressure is the primary cause of instability on this hardware
class, and stability is non-negotiable.

### III. Portability Behind the HAL

Device-specific code MUST live behind the HAL / SDK boundary so the reader core
stays portable across ESP32 e-ink devices (C3, S3, and variants). Pure logic
(parsers, formats, typography, caches) MUST remain host-compilable — no direct
Arduino/hardware includes outside the HAL seam — because host compilability is
both the portability proof and what makes Principle V possible.

Rationale: upstream is deliberately broadening beyond Xteink hardware; code that
only compiles for one board or only on-device resists both goals.

### IV. Evidence Over Claims

Performance and memory claims MUST state their mechanism (allocation count, DRAM
vs flash placement, I/O eliminated) and MUST NOT assert unmeasured numbers.
On-device behavior (e-ink refresh, deep sleep, real heap pressure) is verified on
the device or the official simulator, never assumed from host results. Proposed
changes cite the specific files and lines that justify them. Host-side allocation
counts are the accepted proxy for memory-behavior regressions; wall-clock timing
assertions are opt-in only, never CI gates.

Rationale: AGENTS.md's anti-hallucination and no-unfounded-claims rules exist
because plausible-but-wrong optimizations are cheap to write and expensive to
ship on hardware users cannot easily debug.

### V. Tests Prove Behavior, or They Are Theater (NON-NEGOTIABLE)

All host-reachable logic MUST be covered by the host gtest program
(`bin/run-tests`), and the full program MUST pass both plain and under
ASan+UBSan before any merge into fork `master`. New parser, format, or
cache code lands together with its tests and deterministic fixtures (committed
generators, no randomness). A test only counts if it fails when the behavior it
guards is broken: new suites MUST demonstrate at least one production mutation
they catch. Known-but-unfixed defects are pinned by enabled
"documents current limitation" tests; a fix MUST flip its pin into a regression
guard in the same commit, and the flipped test MUST be shown to fail against the
unfixed code. Host stubs MUST mirror device semantics for any behavior a test
relies on (a guard that passes on host but is dead on device is a defect, not a
fix).

Rationale: this fork's QA program (its host test suites and defect register, now on
fork `master`) surfaced 28 production defects precisely because every suite was
mutation-verified; unverified tests create confidence without safety.

### VI. Untrusted Input Is Hostile

Every byte read from the SD card or the network — EPUB/FB2/XTC containers, CSS,
XML, JSON, fonts, dictionaries, binary caches — is attacker-shaped until
validated. Length and count fields MUST be validated against physical file size
and sane caps BEFORE any allocation they drive. Corrupt caches MUST degrade to
reject-and-rebuild, never crash, never trust. Malformed input MUST produce a
deterministic, bounded failure (no UB, sanitizer-clean, no unbounded
allocation). Every parser MUST have a malformed-input corpus exercising
truncation, lying sizes, hostile nesting, and encoding abuse.

Rationale: on a 380 KB device, a single unvalidated u32 length is the difference
between "book fails to open" and "reader aborts on boot"; upstream's own fix
history shows this is where real bugs live.

### VII. Upstream-First Fork Hygiene

Fork `master` is the fork's integration branch: it carries fork-only work (host test
suites, QA tooling, spec-kit scaffolding, fork features and fixes) on top of upstream.
Commits MUST NOT be made directly on `master` (or on a local `develop`); work lands
only by fast-forwarding or merging a short-lived feature/fix branch that passed the
quality gates, and that branch is deleted after landing. Upstream syncs merge
`origin/develop` (or an upstream `master` release) into `master` through a sync
branch. Fixes MUST be one-defect-one-commit with semantic messages so each is
individually cherry-pickable onto a branch cut from `origin/develop` for an upstream
PR. Upstream PRs stay small (aim under 200 lines of non-test diff), carry semantic
titles, disclose AI usage per the PR template, and never include AI-generated
co-author credits. Test suites and QA tooling stay fork-only — they MUST NOT be
bundled into upstream PR branches. All maintainer-facing communication is written
by the human, not generated. Before starting any work, existing upstream PRs,
issues, and branches MUST be searched to avoid duplicating effort. When
upstream has parallel work in an area (e.g. a pending fix on `develop`), fork
changes MUST mirror upstream's shape so
the next sync merges instead of conflicting. Large refactors get a Discussion
first.

Rationale: the fork's value depends on staying mergeable in both directions;
these norms are the maintainers' explicitly stated preferences. One integration
branch keeps fork work in one place, while PR branches cut from `origin/develop`
keep upstream contributions free of fork-only code.

## Platform Constraints & Standards

- Language: C++20 (`-std=gnu++2a`), no exceptions, no RTTI. The ESP32-C3 is
  single-core and sets the concurrency floor: portable reader-core code MUST NOT
  depend on multi-threading (S3-class targets are dual-core, but code cannot
  assume it); WiFi tasks run on-demand only.
- Display: 800×480 e-ink, single 48 KB framebuffer (`EINK_DISPLAY_SINGLE_BUFFER_MODE`);
  minimize full refreshes; grayscale work requires explicit buffer store/restore.
- Files: `FsFile` destructors auto-close (`DESTRUCTOR_CLOSES_FILE=1`); explicit
  `close()` only before delete, reopen, or at a member's release point. Storage
  goes through `HalStorage` under the storage mutex; SPIFFS is not mounted.
- XML: expat with `XML_GE=0` and `XML_CONTEXT_BYTES=1024`. New host parser suites
  SHOULD compile the in-tree expat with those same flags for device parity; a suite
  that links the system expat instead MUST document the behavioral divergence in a
  source comment (as `test/chapter_html_slim_parser` does today).
- Logging: `LOG_INF`/`LOG_DBG`/`LOG_ERR` only; raw Serial output is deprecated.
- i18n: all user-facing text through `tr()`; log messages may be hardcoded.
- Style: clang-format-21 via `./bin/clang-format-fix` (the only sanctioned entry
  point), 120-column limit. cppcheck (`pio check`) clean at low/medium/high.
- Vendored third-party code: patches stay minimal and provenance-commented;
  vendor-deliberate behavior (e.g. alignment strategies) is documented, not
  rewritten.

## Development Workflow & Quality Gates

Every merge into fork `master` (feature, fix, or upstream sync) MUST pass, in order of
cheapness:

1. `./bin/clang-format-fix -c` — formatting gate.
2. `bin/run-tests` — full host program green (plain).
3. `bin/run-tests --asan` — full host program green under ASan+UBSan.
4. `pio run -e default` — device firmware builds.
5. `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`.
6. For changes touching rendering, input, or activities: verify in the
   crosspoint-simulator (the upstream org's sister repo; its `simulator` envs are
   configured per-developer in gitignored `platformio.local.ini` per AGENTS.md's
   Local Development Configuration — `pio run -e simulator`); for e-ink timing,
   sleep, or memory pressure claims: verify on hardware with serial output.

These gates and their tooling (`bin/run-tests`, `bin/install-hooks`,
`clang-format-fix -c`) live on fork `master` and MUST NOT be carried into upstream
PR branches.
Supporting practice: the opt-in pre-push hook (`bin/install-hooks`) runs gate 1
plus a quick test subset (it skips the two slowest suites; it narrows the loop
and does not replace gates 2–3); specs and plans produced under `.specify/`
MUST carry a Constitution Check against Principles I–VII before implementation
begins; features begin as a
spec (`/speckit-specify`) when they are large enough to need a plan, and may skip
spec-kit ceremony when they are single-commit fixes with an existing pin test.

## Governance

This constitution governs fork work and the spec-kit workflow in this repository.
For upstream-bound contributions, upstream's own documents (AGENTS.md, SCOPE.md,
CONTRIBUTING) are authoritative; where this constitution is stricter, the stricter
rule applies to work done here. AGENTS.md remains the detailed runtime development
guide — this document distills its non-negotiables into reviewable gates and does
not replace it.

Amendments are made by PR editing `.specify/memory/constitution.md`, MUST include
an updated Sync Impact Report, and version per semantic versioning: MAJOR for
principle removals or redefinitions, MINOR for new principles or materially
expanded guidance, PATCH for clarifications. After every upstream sync that
changes AGENTS.md or SCOPE.md, this constitution MUST be reviewed for drift and
amended (or explicitly re-affirmed) in the same branch. Reviews of specs, plans,
and PRs MUST check compliance with Principles I–VII; violations require a
documented justification in the plan's Complexity Tracking table or a change to
this document — silent exceptions are not permitted.

**Version**: 2.0.0 | **Ratified**: 2026-09-14 | **Last Amended**: 2026-09-14
