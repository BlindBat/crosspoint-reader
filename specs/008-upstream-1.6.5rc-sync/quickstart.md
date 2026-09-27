# Quickstart: validating the Upstream 1.6.5rc Sync

Runnable checks, in the order they become possible. Steps 1–5 are agent-verifiable; 6–10 need
the device or a human. Figures go into the release notes (SC-005/006).

## Prerequisites

- Branch `sync/upstream-1.6.5rc`; `git fetch upstream --tags` done (`1.6.5rc` = `a1ceb633`).
- Host toolchain per memory *host-test-toolchain* (Xcode clang via `CC`/`CXX`/`SDKROOT`).
- Device: Xteink X4 with the card that held 944 `.fb2` files (2026-09-22), still running
  `1.6.0-bb.5` for step 6's "before" readings.

## 0. Before flashing anything — the "before" figures (SC-006)

On `1.6.0-bb.5`, one run each, same card: free heap at Home and with an FB2 open (`[MEM]` over
serial), the 24-row FB2 chapter-list window read (ms) and the first open of the 677-chapter
reference book (s), using the boot-time bench from memory *device-bench-harness*. Record them.

## 1. The merge is a merge

```bash
git merge-base --is-ancestor 1.6.5rc HEAD && echo ancestor-ok
git log --oneline --merges -1          # the merge commit
grep -c '^\[env:x4c-gh_release_rc\]' platformio.ini   # 1
grep -n '^version = ' platformio.ini   # 1.6.5rc-bb.1
```

## 2. Host program, plain and sanitised (SC-002)

```bash
bin/run-tests            # expect 100% passed; suite count 69; 5 by-design skips only
bin/run-tests --asan     # same totals, sanitizer-clean
bin/run-tests --filter 'library|fb2'   # the FB2-in-Library work in isolation
```

Record the two totals; the plain total must be ≥ (3,349 − SmartSync cases) + 132.

## 3. Firmware builds and static checks

```bash
./bin/clang-format-fix -c
pio run -e default && pio run -e sticky && pio run -e x4pro && pio run -e x4c && pio run -e papermono
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

If the new pioarduino platform (55.03.311) fails to install, fix the local PlatformIO environment
(memory *platformio-install-quirks*) — not the repo; CI's steps are upstream's and pass at `1.6.5rc`.

## 4. Simulator (gate 6)

`bin/run-simulator`: open Library (all four tabs, collapse a Title group, search), open an FB2
from each tab, open the FB2 reader menu after visiting the chapter list, sleep on a coverless FB2
(stub card). Repeat the Library and FB2 reader screens in all four orientations.

## 5. Docs convergence (SC-008)

```bash
grep -n 'Recent Books' USER_GUIDE.md README.md AGENTS.md      # no top-level screen references
grep -n 'SECTION_FILE_VERSION' AGENTS.md lib/Epub/Epub/Section.cpp   # both say 46
grep -n 'fb2' USER_GUIDE.md | grep -i library                  # .fb2 (fork-only) in the Library section
```

## 6. Device — Library and FB2 (SC-004, FR-020–023, FR-027)

Flash `default` on the X4. Open the Library (first open builds the index — time it with
"Use book metadata" off, then rebuild with it on: **SC-005**, record both and the book count).
- Title tab lists every `.fb2` (count = card scan of `*.fb2`).
- Open one FB2 from Recent, Added, Title and Author → FB2 reader.
- An FB2 that was in Recent before the flash is still in Recent.
- Hold an FB2 row in Recent → removed; delete one from Browse Files → its `fb2_` cache dir is gone.
- Browse Files: an FB2 with an NFD-composed Korean name renders composed.
- FB2 chapter list: hold Down through a repaint; no dropped presses.

## 7. Device — FB2 reader parity (FR-024, FR-025)

- Open the chapter list from the menu, come back, open the menu again: header shows the same
  chapter page as before leaving.
- Reach the end of an FB2; the end-of-book options keep their selection in sync when redrawn.
- Sleep on an FB2 with an embedded cover (cover sleep screen): cover shown with the same threshold
  treatment as an EPUB cover; on a coverless FB2 the stub card appears.

## 8. Device — the "after" figures (SC-006)

Repeat step 0 on the sync build. Every worse figure is attributed in the release notes to a named
upstream change or fixed before release.

## 9. Known unverifiable: X3 anti-aliasing (R5)

No X3 is available. The change mirrors upstream #3439's mechanism in the shared helper; the release
notes state it is unverified on X3.

## 10. Release (SC-007, [contract](contracts/release-pipeline.md))

Land the PR with `gh pr merge --merge`. Tag `1.6.5rc-bb.1`, publish as a **pre-release**. The
workflow attaches exactly five `crosspoint-1.6.5rc-bb.1-<device>.bin`; `strings` each for the tag.
Release notes: figures from steps 0/6/8 against `1.6.0-bb.5`; behaviour-change section (Library
replaces Recent Books; EPUB chapters re-laid out once; RC basis; X3 unverified).
