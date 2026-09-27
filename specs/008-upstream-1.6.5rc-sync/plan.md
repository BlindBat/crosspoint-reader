# Implementation Plan: Upstream 1.6.5rc Sync

**Branch**: `sync/upstream-1.6.5rc` | **Date**: 2026-09-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/008-upstream-1.6.5rc-sync/spec.md`

## Summary

Merge upstream tag `1.6.5rc` into the fork as one merge commit, resolving the 42 conflicted files
toward upstream's shape while keeping the four fork guarantees the spec names (protected paths at
any depth, no aborting allocation, page image bounds, bounded manifest/asset sizes). Then, in five
small follow-up commits on the same branch: teach upstream's new Library to index `.fb2` files
(the one upstream enhancement that otherwise *removes* a fork feature from view), give the FB2
reader the three reader-chrome fixes upstream gave the EPUB reader, give FB2 covers the new sleep
thresholds while keeping the stub fallback, relax one line of upstream's release workflow so
`1.6.5rc-bb.1` pre-releases get their assets, and converge the docs. The fork's 3,349 host tests
plus upstream's 132 are the oracle; the device confirms the Library and FB2 flows and supplies the
before/after figures.

Ponytail scope: no rebase, no cherry-pick reconstruction, no new abstraction. New code exists only
where an upstream enhancement has to reach a fork-only feature: ~40 lines in `LibraryBuilder` +
`Fb2`, ~30 in the FB2 reader, 3 in `Fb2Section`, ~10 in `ReaderUtils`, ~15 across `Fb2`/
`Fb2CoverExtractor`/`SleepActivity`, 1 in `release.yml`.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), `-fno-exceptions`, no RTTI; GitHub Actions YAML;
Python 3 build scripts (unchanged)

**Primary Dependencies**: upstream `1.6.5rc` (`a1ceb633`) and its `freeink-sdk` pin
(`e30d25a0`, 50 commits ahead of the fork's `cb9167d5`, fast-forward); pioarduino 55.03.311 (new
in this range); in-tree expat, miniz, SdFat (`USE_SPI_ARRAY_TRANSFER=1` new); `lib/LibraryIndex`
(new upstream); `lib/hal/HalMemory` (new upstream)

**Storage**: SD — `/.crosspoint/library.idx` (CLX1 v2/fold v3, upstream's), per-book caches
(`epub_` sections rebuild v45 → v46; `fb2_`, `txt_`, `xtc_` unchanged — research R10); FB2 covers
gain the `cover_original.bmp` / `cover_legacy_v2.bmp` names (R6)

**Testing**: host gtest via `bin/run-tests` (plain and `--asan`); 69 suites after reconciliation
(R12); CI `ci.yml` unit-tests matrix (plain + ASan) and five firmware builds; device X4 for the
Library/FB2 flows and figures; simulator for the four orientations

**Target Platform**: ESP32-C3 (`default`) and the four ESP32-S3 boards; the X3 anti-aliasing change
(R5) can only be observed on an X3, which is not available — flagged in quickstart step 9

**Project Type**: embedded firmware fork sync: `lib/`, `src/`, `test/`, `.github/`, docs

**Performance Goals**: FB2 chapter-list window read and first open no worse than `1.6.0-bb.5`
(11 ms / 2.9 s, specs/006 R10) beyond what a named upstream change explains (SC-006); Library index
build time on the 944-FB2 card measured and published (SC-005)

**Constraints**: merge commit landing (FR-001/002); every fork guarantee in FR-004; no test deleted
without replacement (Principle V; the one case is R11); stack locals < 256 B and nothrow
allocations in every new line (Principle II); `lib/Fb2` and `lib/LibraryIndex` stay host-compilable
(Principle III)

**Scale/Scope**: 42 files / 78 hunks of conflict resolution; ~120 new lines of production code;
~10 files of test reconciliation; 5 docs files; 6 commits plus the merge

## Constitution Check

*GATE: checked before Phase 0 research and again after Phase 1 design.*

| Principle | Status | Evidence |
|---|---|---|
| I. Focused reading device | PASS | Every change is upstream's own reading-experience work or the fork's existing FB2/sleep features receiving it. No new UI, theme or connector. |
| II. Memory is the constraint | PASS | New allocations: none on the render path. `Fb2::loadMetadata` runs the existing parser with a 1 KB expat buffer and stops at `</title-info>` (R2); the FB2 chapter position is two cached `int`s that already exist; the arena manifest (upstream's) *reduces* fragmentation. Every `new` in resolved hunks is `makeUniqueNoThrow` (R1: Page.cpp, EpubReaderActivity, FileBrowserActivity, WebDAV handler). |
| III. Portability behind the HAL | PASS | Host-compilability is preserved at every seam the merge touches: `SdCardFont.cpp` keeps `platform::*`, `PngToBmpConverter` gets `platform::taskDelay()` for upstream's `vTaskDelay`, `KOReaderSyncClient`'s `HalMemory` is stubbed on host (R1). `lib/Fb2` and `lib/LibraryIndex` compile on host. |
| IV. Evidence over claims | PASS | Conflict counts, test counts, suite counts and upstream test totals are measured (research header, R12). Timings and heap are measured before/after on the device, never asserted (SC-005/006). The X3 claim is explicitly marked unverifiable locally (R5). |
| V. Tests prove behavior | PASS (obligation) | Oracle: 3,349 fork tests + 132 upstream, plain and ASan (both green on the branch today). New code lands with tests and a named mutation each: FB2 in `library_builder` (remove `.fb2` from `isBookName`), metadata-only stop in `fb2_metadata_parser` (remove the stop → sink fires), font-cache release in `fb2_section_cache` (drop the call → count 0), FB2 percent math in `xtc_fb2_readers`, cover flag pass-through in `fb2_cover_extractor`. Pin flips name their upstream change (R11). |
| VI. Untrusted input | PASS | Every fork validation guard survives (R1: Page image bounds, `truncateAtInvalidUtf8`, bounded manifest sizes, WebPathUtils). FB2 metadata for the index goes through the same validated cache reader or the same expat parser with the same caps. |
| VII. Upstream-first hygiene | PASS | True merge on a sync branch, landed as a merge commit (Q1). Conflicts resolved to upstream's shape wherever both sides solved the same problem (R1). Follow-ups are one logical change each; FB2 work is fork-only by nature. Upstream `AGENTS.md`/`SCOPE.md` unchanged in the range → constitution **re-affirmed** here, no amendment (R14). Parallel work on `upstream/develop` checked for every file the new code touches — no shape to mirror (notes in R2, R5, R9). Test suites and QA tooling stay fork-only. |

**Gate result**: PASS, no violations.

### Post-design re-check (after Phase 1)

Still PASS. Design added two obligations, both in the contracts:
- **III/V**: `Fb2::loadMetadata` must compose NFC and must never build the chapter index; the
  contract's "never opens `sections/`, never writes `book.bin`" clause is what the `library_builder`
  parse counter and the `fb2_metadata_parser` stop test check.
- **VII**: the single edit to upstream's `release.yml` is recorded in
  [contracts/release-pipeline.md](contracts/release-pipeline.md) so the next sync re-applies it
  knowingly rather than rediscovering it.

## Project Structure

### Documentation (this feature)

```text
specs/008-upstream-1.6.5rc-sync/
├── spec.md
├── plan.md              # this file
├── research.md          # R1–R14 (conflict table is R1)
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── fb2-load-metadata.md
│   └── release-pipeline.md
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks output
```

### Source Code (repository root)

```text
# merge commit (conflict resolution only — 42 files, research R1)
.github/workflows/release.yml           # upstream verbatim (relaxation = separate ci: commit, T047)
platformio.ini                          # version = 1.6.5rc-bb.1
lib/EpdFont/SdCardFont.cpp              # upstream logic on the platform seam
lib/Epub/Epub/Page.cpp                  # nothrow + fork image bounds
lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp   # borrowed attrs + hidden flag
lib/FsHelpers/FsHelpers.cpp             # both helpers
lib/GfxRenderer/{Bitmap.cpp,BitmapHelpers.h}      # upstream
lib/hal/HalStorage.cpp                  # guards + modificationTime
lib/I18n/translations/english.yaml      # union
lib/JsonParser/ReleaseJsonParser.h, src/network/OtaUpdater.cpp   # single layout, heap parser
lib/KOReaderSync/KOReaderSyncClient.cpp # HalMemory
lib/Platform/PlatformSeam.{h,cpp}       # + taskDelay()
lib/PngToBmpConverter/PngToBmpConverter.cpp       # thresholds, RAII kept, seam delay
src/SettingsList.h                      # toggle inside buildBaseSettingsList()
src/activities/{ActivityManager.cpp,boot_sleep/SleepActivity.cpp,home/*,reader/*}  # per R1
src/activities/settings/FontDownloadActivity.{cpp,h}, src/util/FontManifest.{h,cpp} # arena in the seam
src/components/themes/BaseTheme.{cpp,h} # static + const char*
src/network/{CrossPointWebServer.cpp,WebDAVHandler.cpp,WebDAVHandler.h}          # WebPathUtils + ETag
test/CMakeLists.txt, test/sd_card_font/, test/content_opf_parser/, test/chapter_html_slim_parser/,
test/ota_asset_selection/, test/release_json_parser/, test/font_system/, test/library_helpers/,
test/kosync_client/, test/chapter_xpath_resolver/, test/progress_mapper/   # reconciliation (R12)
docs/file-formats.md, USER_GUIDE.md, ROADMAP.md

# follow-up commits
lib/LibraryIndex/LibraryBuilder.cpp     # isBookName + FB2 metadata branch
lib/Fb2/Fb2.{h,cpp}                     # loadMetadata(title, author); cover threshold arg
lib/Fb2/Fb2/Fb2MetadataParser.{h,cpp}   # metadata-only stop
lib/Fb2/Fb2/Fb2CoverExtractor.{h,cpp}   # forward originalThresholds
lib/Fb2/Fb2/Fb2Section.cpp              # release SD font caches before layout
src/activities/reader/Fb2ReaderActivity.{h,cpp}, Fb2ReaderMath.{h,cpp}   # ChapterPosition parity
src/activities/reader/ReaderUtils.h     # cleanup-refresh grayscale preconditioning
src/activities/boot_sleep/SleepActivity.cpp       # FB2 branch threshold arg
test/library_builder/{stubs/Fb2.h,LibraryBuilderTest.cpp}, test/fb2_metadata_parser/,
test/fb2_section_cache/, test/xtc_fb2_readers/, test/fb2_cover_extractor/
AGENTS.md, USER_GUIDE.md, specs/001-crosspoint-reader-baseline/spec.md
```

**Structure Decision**: existing layout; no new directories in `lib/` or `src/`. The only new
files are test stubs and the two spec contracts.

## Commit plan (one logical change each, Principle VII)

1. `Merge tag '1.6.5rc' into sync/upstream-1.6.5rc` — the 42 resolutions of R1, the test
   reconciliation of R12, and nothing else (`release.yml` resolves to upstream verbatim; its one-line
   relaxation is the separate `ci:` commit in tasks T047). Green: `bin/run-tests`, `--asan`, `pio run -e default`.
2. `feat(library): index FictionBook 2 files` — R2 + its tests.
3. `fix(fb2): keep the reader menu on the cached chapter position` — R3 + test.
4. `fix(fb2): release SD font caches before laying out a chapter` — R4 + test.
5. `fix(reader): precondition grayscale on cleanup refreshes in the base-display helper` — R5.
6. `fix(sleep): render FB2 covers with the panel thresholds` — R6 + test (stub fallback is already
   in the merge commit's `SleepActivity` resolution).
7. `docs: converge the guides and the baseline spec with the merged firmware` — R14.

The PR lands with `gh pr merge --merge` (FR-002). The release is then tag `1.6.5rc-bb.1` published
as a pre-release; no version commit is needed because the merge commit already set the version line.

## Complexity Tracking

> No Constitution violations. Recorded here: the deliberate choices and their ceilings.

| Decision | Why | Simpler / other alternative rejected because |
|---|---|---|
| Move upstream's arena manifest parse into the fork's `FontManifest` seam instead of taking `FontDownloadActivity` verbatim | 26 host tests cover the manifest parser; Principle V forbids deleting them without replacement, and upstream has none for the arena. | Taking upstream inline deletes coverage; keeping the fork's `std::string` model rejects upstream's fragmentation fix and re-conflicts every sync. |
| Take upstream's dither fail-on-OOM over the fork's fall-back to plain quantisation | `Bitmap*.{cpp,h}` is upstream-hot (two commits in this range); the fork's nicety had no test pinning it. | Keeping the fall-back re-conflicts on every sync for a behaviour nobody measured. |
| X3 cleanup-refresh sequencing (`preconditionGrayscale()`) in the shared `displayBaseWithRefreshCycle` helper | FB2 and TXT get upstream's #3439 fix through the one helper they already call. | Hardware-bound: the host test pins the call order only; unverified without an X3 (disclosed in the release notes). |
| Drop the fork's dual OTA asset layout | `releases/latest` is the newest upstream release, which uses the new names; the old layout can never be "latest" again. | Keeping both doubles the parser's asset buffers (the fork moved the parser to the heap because of them). |
| Fix X3 preconditioning in `displayBaseWithRefreshCycle`, not in the FB2 reader | One helper serves FB2, TXT and one EPUB path; the fix is the same three lines for all. | Per-reader copies of the EPUB branch (tiled/overlap logic the FB2 reader cannot use). |
| No forced Library rebuild when an index lacks FB2 rows | Only a card that ran stock `1.6.5rc` can be in that state; the user's Rebuild setting exists. | A fold-version bump rebuilds every user's index once for a case few will hit (`ponytail:` upgrade path if a report arrives). |
| Delete `SmartSyncDecision` and its tests with it (R11) | The code under test is replaced by upstream's `ProgressComparison`, which ships its own 11 tests. | Keeping both decision models is two answers to one question. |
