# Research: Upstream 1.6.5rc Sync

**Date**: 2026-09-27 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

Every number below was measured on 2026-09-26/27 against fork `master` `b68be959` and upstream tag
`1.6.5rc` (`a1ceb633`) unless a source is named. The trial merge is `git merge --no-commit --no-ff
1.6.5rc` on a detached worktree of `master`; it is reproducible and was run twice with identical
results.

## R1. Conflict inventory and per-file resolution

**Decision**: one merge commit resolves all 42 conflicted files toward upstream's shape (Constitution
VII), keeping the four fork guarantees FR-004 names. The table below is the resolution the merge
commit must produce; anything not listed auto-merged cleanly (including `platformio.ini`'s
environments, `ci.yml`, all 33 non-English translations, `main.cpp`, `HomeActivity.cpp`,
`Section.cpp`, `CssParser.*`, `GfxRenderer.*`, `HalDisplay.*`, `Utf8.*`).

Legend — **U**: take upstream · **F**: take fork · **U+F**: upstream's shape plus the named fork
guarantee · **D**: delete.

| File | Hunks | Resolution | Fork guarantee kept / note |
|---|---|---|---|
| `.github/workflows/release.yml` | 1 | U (verbatim) | The fork's one-line version-check relaxation is its own commit (tasks T047, R9), so the deviation from upstream stays visible for the next sync. |
| `platformio.ini` | 1 | `version = 1.6.5rc-bb.1` | The fork's next version (clarification Q2); every environment is already defined once (`grep -c '^\[env:x4c-gh_release_rc\]'` = 1 after auto-merge). |
| `ROADMAP.md` | 2 | U | Upstream's document; the fork's edits were style convergence. |
| `USER_GUIDE.md` | 2 | U + `.fb2` *(fork-only)* in the Library's format list | "Check for updates" takes upstream's wording (asset naming changed). |
| `docs/file-formats.md` | 1 | U+F | Both additions: CLX1 + v46 (upstream), TextBlock limits / CSS cache / FB2 / TXT (fork). |
| `lib/EpdFont/SdCardFont.cpp` | 3 | U+F | Upstream's `accumulate` flag and `readOrder.reset()`; calls stay on `platform::freeHeap()` / `platform::millis()` so the file remains host-compilable (Principle III). |
| `lib/Epub/Epub/Page.cpp` | 3 | U+F | Upstream's `makeUniqueNoThrow<PageImage>`; the fork's image-dimension guard (`MAX_PAGE_IMAGE_EDGE`) stays in front of it (FR-004c). |
| `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp` | 1 | F + upstream's `hasHiddenAttr` | Borrowed `const char*` attributes (no per-element `std::string`), plus the new hidden flag. |
| `lib/FsHelpers/FsHelpers.cpp` | 1 | U+F | Both helpers: the fork's `truncateAtInvalidUtf8` (FAT32 fix) and upstream's `isSafePathComponent`. |
| `lib/GfxRenderer/Bitmap.cpp`, `BitmapHelpers.h` | 1 + 6 | U | `isValid()`, `originalThresholds`, and upstream's fail-on-OOM (`BmpReaderError::OomDitherer`) replace the fork's silent fall-back to plain quantisation. Behaviour change accepted for mergeability; any fork test pinning the fall-back flips (none found by name: `grep -rl 'dithering disabled' test` → 0). |
| `lib/hal/HalStorage.cpp` | 1 | U+F | Upstream's `modificationTime()` (the Library needs it) beside the fork's null-`impl` guards. |
| `lib/I18n/translations/english.yaml` | 1 | U+F | Upstream's `STR_INVALID_FONT_MANIFEST` replaces the fork's `STR_FONT_MANIFEST_INVALID`; the fork's other `STR_FONT_*` keys stay (translated errors, FR-010). |
| `lib/JsonParser/ReleaseJsonParser.h`, `src/network/OtaUpdater.cpp` | 1 + 2 | U+F | Upstream's single `crosspoint-<tag>-<device>.bin` layout (drop the fork's `alternateAssetName` dual layout — `releases/latest` is always the newest upstream release, which uses the new names); the fork's heap-allocated parser stays (2 KB task stack, FR-004d). Fork OTA tests re-target the new names (R12). |
| `lib/KOReaderSync/KOReaderSyncClient.cpp` | 1 | U | `HalMemory::getDefaultHeap()`; the `kosync_client` host suite gains a `HalMemory` stub (as upstream's `inflate_stream` stubs `esp_heap_caps.h`). |
| `lib/PngToBmpConverter/PngToBmpConverter.cpp` | 4 | U+F | Upstream's thresholds and fail-on-OOM; the fork's RAII row buffers stay (upstream's manual `free()` chains are moot); upstream's `vTaskDelay(1)` in `yieldDuringDecode` goes through a new `platform::taskDelay()` seam call (host no-op) so `png_decode` still compiles. |
| `src/SettingsList.h` | 1 | F + upstream's toggle | `libraryUseMetadata` toggle added inside the fork's `buildBaseSettingsList()` (tested seam). |
| `src/activities/ActivityManager.cpp` | 1 | U | `goToLibrary()` with its OOM check. |
| `src/activities/boot_sleep/SleepActivity.cpp` | 1 | U+F | Upstream's `originalThresholds`; a failed cover generation **falls through to the stub card**, not `renderNoCoverSleepScreen` (FR-025). FB2 branch gains the same argument (R6). |
| `src/activities/home/FileBrowserActivity.cpp` | 1 | U+F | `makeUniqueNoThrow` + `utf8ComposeNfc(entry)`. |
| `src/activities/home/HomeActivity.h` | 2 | F | Keep `HomeMenuMap.h` (tested, `test/library_helpers`); rename `RECENTS` → `LIBRARY` there and in its test; drop upstream's in-class copy of the same two functions. |
| `src/activities/home/RecentBooksActivity.cpp` | UD | D | The fork's only change was `makeUniqueNoThrow`; the screen is gone. |
| `src/activities/reader/EpubReaderActivity.cpp` | 2 | U+F | Upstream's `ChapterPosition` / new `KOReaderSyncActivity` constructor, with `makeUniqueNoThrow` in place of `std::make_unique`. |
| `src/activities/reader/KOReaderSyncActivity.cpp` | 4 | U | Upstream's `ProgressComparison` replaces the fork's `SmartSyncDecision` (R11). |
| `src/activities/reader/TxtReaderActivity.h` | 1 | U | Non-const renderer (upstream's layout path needs it). |
| `src/activities/reader/XtcReaderChapterSelectionActivity.cpp` | 1 | F | `xtc_reader::findChapterIndexForPage` is the same algorithm with a host test; upstream inlined it. |
| `src/activities/settings/FontDownloadActivity.{cpp,h}` | 9 + 1 | U+F | Upstream's string arena and `StrRef` records are the data model; the parse moves into the fork's `src/util/FontManifest.{h,cpp}` seam so its 26 host tests re-target the arena API instead of dying; errors keep `tr()` + `setFormattedError` (FR-010, i18n rule). See Complexity Tracking. |
| `src/components/themes/BaseTheme.{cpp,h}` | 1 + 1 | U+F | `static` (upstream) with `const char* title` (fork: no status-bar string copy per render). |
| `src/network/CrossPointWebServer.cpp` | 9 | F + upstream additions | Keep `WebPathUtils` (every-component protection, FR-004a; tested in `network_helpers`) and the nothrow `WebDAVHandler` allocation (FR-004b); add upstream's `If-None-Match` collection, ETag/Cache-Control static handlers and the escaped files page; drop upstream's inline `normalizeWebPath` / `isProtectedItemName`. `parseWsStart` must reject what `isSafePathComponent` rejects (`/`, `\`, `.`, `..`, empty). |
| `src/network/WebDAVHandler.{cpp,h}` | 1 + 1 | F | `WebPathUtils::mimeTypeForPath` already replaced `getMimeType`; upstream's re-added copies are dropped. |
| `test/CMakeLists.txt` | 1 | F + 11 new `crosspoint_suite()` lines − `sdcard_font` | R12. |
| `test/chapter_html_slim_parser/CMakeLists.txt` | 1 | U+F | `AllocCounter.cpp` (fork) plus real `Page.cpp`, `TextBlock.cpp`, `BidiUtils.cpp`, `minibidi.c` (upstream); `ParserLinkStubs.cpp` drops the `TextBlock`/`Page*` stubs those sources now define. |
| `test/content_opf_parser/*` | 6 AA | F + upstream's 5 tests | The fork's 57-test suite (plus `ContainerParserTest`, `TocParsersTest`) is the base; upstream's five `ContentOpfParserMetadata` tests and the stubs they need (`stubs/Serialization.h`, `stubs/FsHelpers.h`) are added. |

**Rationale**: every U+F row is a guarantee the spec names or a host-compilability seam Principle III
requires; every plain U row is a place where the fork's variant added nothing upstream's lacks.

**Alternatives considered**: rebase (loses ancestry, FR-001); cherry-picking the 31 upstream commits
(same loss, 31× the conflict work); keeping fork variants where upstream differs "because they are
tested" (re-conflicts on every future sync — Constitution VII forbids).

## R2. The Library does not know FB2

**Decision**: add `.fb2` to `isBookName()` (`lib/LibraryIndex/LibraryBuilder.cpp:178-179` at
`1.6.5rc`) and extend `extractionExpected` (line 297) to FB2, backed by a new
`Fb2::loadMetadata(std::string& title, std::string& author)` with the same contract as
`Epub::loadMetadata` ([contract](contracts/fb2-load-metadata.md)).

**Evidence**: `git grep -i fb2 1.6.5rc -- src lib` matches only font binaries; the Library opens a
row through `Activity::onSelectBook(path)` → `activityManager.goToReader(path)`
(`src/activities/Activity.cpp:15` at `1.6.5rc`), and the fork's reader factory already routes
`.fb2` (`src/activities/reader/ReaderActivity.cpp:34`), so opening, "remove from Recent" and
delete-with-cache (`clearBookCache`, fork `BookCacheUtils.cpp:32`) need no further work once the
file is indexed.

**How `loadMetadata` stays cheap**: `Fb2::loadMetadataCache()` (`lib/Fb2/Fb2.cpp:180`) already reads
title and author from `book.bin` v5 before the chapter records; when there is no cache the parser
runs in a new metadata-only mode that calls `XML_StopParser(parser, XML_FALSE)` at `</title-info>`
(`Fb2MetadataParser.cpp:199`) and treats `XML_ERROR_ABORTED` as success. `<title-info>` sits inside
`<description>` ahead of `<body>` in every FB2, so the read is the file head only — the same shape as
upstream stopping the OPF parser before the manifest. Title and author are NFC-composed as upstream
does for EPUB (`utf8ComposeNfc`, `Epub.cpp` at `1.6.5rc`).

**Parallel work on `develop` (Constitution VII, checked 2026-09-27)**: `1.6.5rc..upstream/develop` changes
`LibraryBuilder.cpp` by +31 lines (#3608 "refresh library") but `isBookName()` is byte-identical, so the
`.fb2` clause has no other shape to mirror.

**Alternatives considered**: `Fb2::load(true)` (builds the whole chapter index for every
never-opened FB2 on the card — 22–45 s per book on the C3 per the bench memory; rejected);
folder/filename-only for FB2 (fails FR-021).

## R3. FB2 reader parity — reader-menu chapter position

**Decision**: mirror upstream #3437 exactly. `Fb2ReaderActivity` gains `chapterPosition()` returning
`{section->currentPage, section->pageCount}` when the section is live and
`{nextPageNumber, cachedSectionTotalPageCount}` after a child screen released it, plus
`bookPercentFor()`. The FB2 reader already has the two cached fields (`Fb2ReaderActivity.h:25,29`)
and the same `section.reset()`-then-`openReaderMenu()` pattern (`Fb2ReaderActivity.cpp:180-227`),
so it has the same defect upstream fixed. `ChapterPosition.h` is the shared upstream header (its
`chapter_position` suite covers the struct); the percent arithmetic lands in `Fb2ReaderMath` so it is
host-tested (`xtc_fb2_readers`).

## R4. FB2 reader parity — font caches before layout

**Decision**: `Fb2Section::startBuild` releases rebuildable SD-font caches the way `Section.cpp`
does after #3527 (`renderer.getFontCacheManager()->releaseSdFontCaches()`, three lines).
`Fb2Section` already holds `GfxRenderer& renderer` (`Fb2Section.h:27`). Verified by the
`fb2_section_cache` suite with a counting renderer stub (mutation: drop the call → count stays 0).

## R5. FB2 reader parity — X3 anti-aliasing

**Decision**: fix it once in the shared helper, not per reader. `ReaderUtils::displayBaseWithRefreshCycle`
is the base-display step for the FB2 reader (`Fb2ReaderActivity.cpp:666`), the TXT reader and one
EPUB path (`EpubReaderActivity.cpp:1585` at `1.6.5rc`). Upstream's fix (#3439) lives only in
`EpubReaderActivity`'s non-tiled branch: on a cleanup refresh, `displayBuffer(HALF_REFRESH)` then
`renderer.preconditionGrayscale()` before the grey planes; otherwise `displayGrayscaleBase(FAST_REFRESH)`.
The helper's non-combined branch adopts that sequence (`preconditionGrayscale()` exists on both the
renderer and the HAL, and is a no-op on X4 — `lib/hal/HalDisplay.h:78-80`).

**Parallel work on `develop` (Constitution VII, checked 2026-09-27)**: `ReaderUtils.h` changes by 94 lines on
`develop` (#3586 CrossInk tap controls, #3709 RTL tap zones) — the touch helpers, not
`displayBaseWithRefreshCycle`; no other shape to mirror.

**Verification limit**: the fork owner's device is an X4 (memory: device-measurement). The change
reproduces upstream's mechanism, but the X3 outcome cannot be observed locally; the release notes
say so (quickstart step 9).

**Alternatives considered**: copying the whole EPUB display branch into the FB2 reader (duplicates
the tiled/overlap logic the FB2 reader never uses); doing nothing (FR-024).

## R6. Sleep cover parity

**Decision**: `Fb2::generateCoverBmp(bool originalThresholds)` / `getCoverBmpPath(bool)` mirror
`Epub`'s (`cover_original.bmp` / `cover_legacy_v2.bmp`), `Fb2CoverExtractor::extract` forwards the
flag to `JpegToBmpConverter::jpegFileToBmpStream(…, crop, originalThresholds)` (the converter already
takes it at `1.6.5rc`), and `SleepActivity`'s FB2 branch passes the same `originalThresholds` value
upstream computes for EPUB (`SleepActivity.cpp:806` at `1.6.5rc`: SSD1677 panel, grayscale
supported, no cover filter). The old `cover.bmp` is simply never read again; it is regenerated once
under the new name.

**Stub fallback**: upstream's EPUB branch now returns to the no-cover screen when generation fails;
the fork's rule (specs/002) is that a failed or missing cover shows the stub card. The merge keeps
the fork's flow: on failure `coverBmpPath` stays empty and control reaches
`renderCoverStubSleepScreen` (`SleepActivity.cpp:704` on the fork). Applies to EPUB, FB2 and TXT
alike.

## R7. End-of-book synchronisation and other inherited fixes

**Decision**: nothing to write. The FB2 reader's end-of-book screen is rendered by the shared
`ReaderActivity` base (`Fb2ReaderActivity.cpp:409`, `ReaderActivity.cpp:181`), and upstream's fix
(#3418) changed `EndOfBookOptions.{h,cpp}`, which auto-merged. Likewise the FB2 chapter list is a
`UiListActivity` (`Fb2ReaderChapterSelectionActivity.h:11`), so #3534 (presses kept during
repaint) applies by inheritance, and NFD filename rendering (#3036) is in `FileBrowserActivity`
regardless of extension. FR-027 asks these be *verified*, not assumed — quickstart steps 6–7.

## R8. Absolute image grayscale and the FB2 reader

**Decision**: not applicable. The FB2 reader renders text only and its cover page is drawn
black-and-white by design (`Fb2ReaderActivity.cpp:79`, a `ponytail:` note naming the upgrade path).
Upstream's UC8279 absolute-grayscale image page (#3478) has no FB2 counterpart to reach.

## R9. Release pipeline for `-bb` tags

**Decision**: adopt upstream's `release.yml` and reduce its version check to `test "$version" =
"$configured_version"` for pre-releases and releases alike. Upstream's rule for a pre-release is
`tag == <version>rc`; the fork's RC-based name `1.6.5rc-bb.1` carries its `rc` inside the version, so
without the edit a pre-release would build nothing and the release would sit empty (the check runs
before the build). The `prerelease` flag still selects the `*-gh_release_rc` environments, whose
`-rc+<sha>` stamp yields a firmware string like `1.6.5rc-bb.1-rc+a1b2c3d`; it contains the tag
(SC-007) and `OtaUpdater::isUpdateNewer` parses it (`sscanf("%d.%d.%d")` stops at `rc`, and the
`-rc` substring makes an equal-numbered upstream final an offered update — the intended semantics,
`OtaUpdater.cpp:104-157`). `release_candidate.yml` (workflow_dispatch on `release/*` branches)
stays as upstream ships it; the fork does not use it.

**Parallel work on `develop` (Constitution VII, checked 2026-09-27)**: `release.yml` changes by 2 lines on
`develop` (#3545, an action version bump); the version check is untouched, so the fork's one-line edit
re-applies cleanly.

**Alternatives considered**: publishing RC-based fork releases as normal releases (works with no
edit, but the user chose the pre-release flag — clarification Q2); keeping the fork's tag-push
artifact workflow (manual upload of five renamed files; rejected by Q2).

## R10. Caches after the sync

**Decision**: no FB2, TXT or XTC version bump. Section cache v46 "keeps the version 45 serialized
layout unchanged" (`docs/file-formats.md` at `1.6.5rc`); the `Page.cpp` change (#3518) is
ownership only. FB2 section files therefore still deserialise; EPUB section caches rebuild once
(v45 → v46, upstream's decision). `CSS_CACHE_VERSION` stays 12. The Library index format is
upstream's (`CLIX_FORMAT_VERSION` 2, `CLIX_FOLD_VERSION` 3); an index built by stock `1.6.5rc`
carries no FB2 rows and the same versions, so nothing forces a rebuild — the user's
**Rebuild library index** does (spec edge case; release notes).

## R11. KOSync: fork `SmartSyncDecision` vs upstream `ProgressComparison`

**Decision**: upstream's model. `KOReaderSyncActivity` now keeps the alternate-hash progress and
compares *mapped positions* (#3111, `lib/KOReaderSync/ProgressComparison.{h,cpp}`, tested by
`progress_comparison`, 11 tests). The fork's `SmartSyncDecision.{h,cpp}` — the same decision in the
fork's shape — is deleted together with its tests in `kosync_client`; the replacement coverage is
upstream's suite. This is the one place the sync removes tests with the code they exercised
(FR-012's "deliberately changed by the sync"). The fork's "documents current limitation" pins in
`test/chapter_xpath_resolver` and `test/progress_mapper` are checked one by one against #3174's
`ChapterXPathResolver` changes and flipped where `1.6.5rc` delivers the upstream-expected value
named in each pin's comment.

## R12. Test program reconciliation

**Decision**:
- `test/CMakeLists.txt` keeps the fork's `crosspoint_suite()` registry (it powers `--filter`) and
  adds the 11 upstream suites; `sdcard_font` is removed.
- `sd_card_font` (upstream) absorbs the fork's `sdcard_font` tests: both define target
  `SdCardFontTest`, so two directories cannot coexist. The other pairs have distinct targets
  (`ChapterXPathResolverTest`/`KOReaderXPathResolverTest`, `ProgressMapperTest`/`ProgressComparisonTest`,
  `WebDavPathsTest`/`FsHelpersTest`) and stay separate — different code under test, not duplicates.
- `content_opf_parser`: R1.
- `ota_asset_selection` (34 old-name references across 4 files) and `release_json_parser` (37)
  re-target `crosspoint-<tag>-<device>.bin`; upstream's 52-line addition to
  `ReleaseJsonParserTest.cpp` merges in.
- `font_system/FontManifestTest.cpp` (26 tests) re-targets the arena API (R1).
- `library_helpers` renames `RECENTS` → `LIBRARY`.
- Upstream's `library_builder` gets a `stubs/Fb2.h` mirroring its `stubs/Epub.h` (`FakeMetadata`
  map + parse counter) and three FB2 cases: indexed, metadata extracted when enabled, parse failure
  falls back to the filename stem. `fb2_metadata_parser` gets "metadata-only parse stops before
  `<body>`" (the sink is never called; mutation: remove the stop and the sink fires).

**Counts**: upstream's 11 suites hold 132 test cases (`TEST(`/`TEST_F(` count at `1.6.5rc`). The
program after reconciliation: 59 − 1 + 11 = **69 suites**; tests ≈ 3,349 − SmartSync cases + 132 +
the new FB2 cases, measured at the end (SC-002 floor: every surviving fork test + 132).

## R13. Toolchain

**Decision**: accept upstream's pioarduino 55.03.311 and `firmware_tuned_c3` as merged (auto-merge
was clean). Risk: the fork's PlatformIO install is pinned and needed injected ESP32 deps once
(memory: platformio-install-quirks). The first implementation task is `pio run -e default` on the
merge commit; CI's five builds use upstream's proven `ci.yml` steps. The simulator (gate 6) is a
sister repo maintained by upstream against this range.

## R14. Documentation drift the merge creates

**Decision**: fix in one `docs:` commit after the merge —
- `AGENTS.md` (staged as `AGENTS.md`; `CLAUDE.md` is a symlink): `.crosspoint/` list gains
  `library.idx` (line 169); cache table `SECTION_FILE_VERSION` 45 → 46 (line 1080) and the example
  bump text (line 1101); directory structure gains `lib/LibraryIndex/` and `src/activities/library/`;
  HAL table gains `HalMemory`; any "Recent Books" screen reference → Library.
- `USER_GUIDE.md`: R1.
- `specs/001-crosspoint-reader-baseline/spec.md`: FR-029, FR-037, FR-038, US2 scenario 5 and the
  `RecentBook` entity gain a "superseded by the Library — see specs/008" note; the text is not
  rewritten.
- Constitution: upstream `AGENTS.md`/`SCOPE.md` unchanged in the range (`git diff --stat 54337e6d
  1.6.5rc -- AGENTS.md SCOPE.md` is empty) → re-affirmed in the plan's Constitution Check, no
  amendment.

## R15. Device and build figures (2026-09-27)

Firmware, one build per environment at the merge commit `0d7d98a7` (+ the one-line `uninitvar` fix):

| Environment | RAM | Flash |
|---|---|---|
| `default` (C3) | 17.3 % — 56,616 B of 327,680 | 85.4 % — 5,593,701 B of 6,553,600 |
| `sticky` | 20.4 % — 66,720 B | 82.0 % — 5,372,767 B |
| `x4pro` | 30.6 % — 100,240 B | 83.7 % — 5,485,470 B |
| `x4c` | 30.5 % — 100,088 B | 83.3 % — 5,461,519 B |
| `papermono` | 35.6 % — 116,656 B | 83.8 % — 5,492,294 B |

`1.6.0-bb.5` for comparison, from the fork's `Compile Release` run 36041527400 (the `*-gh_release` envs, LOG_LEVEL=1, so a
few hundred bytes lighter than the dev envs above): `gh_release` 17.2 % RAM (56,284 B) / 83.5 % Flash (5,473,571 B);
`sticky` 20.2 % / 80.5 %; `x4pro` 27.4 % / 82.1 %; `papermono` 32.4 % / 82.2 %. The sync adds ~0.3 KB RAM and
~100–120 KB flash per board (Library index, arena manifest, upstream 1.6.1–1.6.5rc).
`pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`: clean after the T024 fix.
Host program at the merge: 3,484 tests, plain and ASan; after US3 (T031–T044): +12 tests, the ten touched suites green,
every planned mutation kills exactly its own test (isBookName, extractionExpected, failure branch, title-info stop,
loadMetadata via load(), font-cache release, fraction clamp, threshold flag).

Toolchain note: platform 55.03.311's SCons needs the pioarduino platformio-core fork (v6.1.19 zip) in **both** the
`pio` venv and `~/.platformio/penv` — `sticky`'s hybrid build re-invokes `<penv>/bin/pio run -e sticky`, and the
stock core there fails with `No module named 'SCons.Tool.FortranCommon'`.

Device figures (SC-006, human — T001 before, T054 after):

| Figure | `1.6.0-bb.5` | sync build |
|---|---|---|
| Free heap at Home | | |
| Free heap with an FB2 open | | |
| 24-row FB2 chapter-list window read (ms) | | |
| First open of the 677-chapter reference book (s) | | |

US2 audits (2026-09-27): T027 (a) `pathHasProtectedComponent` guards six web-server sites (list, create, delete,
folder create, WS upload, command path); (b) no bare `new` outside vendored code (`grep -rn 'new [A-Z]…' src lib`
matches only a log string); (c) `MAX_PAGE_IMAGE_EDGE` check at `Page.cpp:105` precedes `makeUniqueNoThrow<PageImage>`
at `:111`; (d) `FONT_MANIFEST_MAX_SCRIPT_GROUPS` and the `crc32` type check are in `src/util/FontManifest.cpp`, and
`test/release_json_parser/ReleaseJsonParserCorpusTest.cpp` still rejects a 26-digit asset size. T028: all 24 suites of
the inventory pass. T030: the 15 corpus suites pass under ASan with no sanitizer output.
T048: `strings .pio/build/default/firmware.bin | grep -o '1\.6\.5rc-bb\.1[^ ]*'` → `1.6.5rc-bb.1-dev-sync/upstream-1.6.5rc-5fc49412`
(the development stamp; the RC envs' `-rc+<sha>` form is CI-only). Note: `pio check` deletes the `default` build products,
so run `strings` before it.

T052 (final tree, `d0…` = HEAD after the style/override commits): `./bin/clang-format-fix -c` clean — six merge-resolved
files needed one `style:` commit (`-g` had skipped them because they were staged when it ran); `bin/run-tests` 3,496 /
3,496 and `--asan` 3,496 / 3,496 (SC-002 floor 3,484 + 12); `pio run` for `default` 17.3 % / 85.4 % (56,616 B /
5,594,683 B), `sticky` 20.4 % / 82.0 %, `x4pro` 30.6 % / 83.7 %, `x4c` 30.5 % / 83.4 %, `papermono` 35.6 % / 83.8 %;
`pio check` (Cppcheck 2.20.0 with platform 55.03.311) clean after marking `~OpdsParser()` `override`, the one
`[low:style]` finding. SC-009: `git diff --stat 0d7d98a7..HEAD` lists only the Apply areas (`lib/Fb2`, `lib/LibraryIndex`,
`src/activities/reader/Fb2*`, the shared `ReaderUtils.h` helper — see the plan's Complexity Tracking — `SleepActivity.cpp`,
`release.yml`, tests), docs, the formatting commit and the `override` one-liner.

T053: the simulator (`bin/run-simulator --build-only`, `.pio/build/simulator/program`) builds from the final tree; the
visual walk (Library tabs / collapse / search, FB2 from each tab, FB2 menu after the chapter list, coverless FB2 sleep
stub, four orientations) needs a person at the window — outcome to be noted here.

## R16. Analyze findings and dispositions (2026-09-27)

`/speckit-analyze` raised 11 findings, none critical; walked one by one with the user:

| ID | Finding | Disposition |
|---|---|---|
| I1 | contract vs T034 on an FB2 without `<title-info>` | closed — `loadMetadata` returns `true` with empty strings; contract row rewritten |
| A1 | T037 "same value as the status bar" vs upstream's `chapterFraction()` | closed — upstream's 0-based fraction; status bar untouched |
| I2 | FR-012 forbade the R11 test deletion | closed — FR-012 gained the replacement-coverage carve-out |
| G1 | FR-006 (pre-sync caches) had no task | closed — first check in T026 |
| G2, G4, G5 | ASan CI matrix, SC-008/SC-009 checks, idle-wake on FB2 | closed — lines added to T021, T045, T051, T055 |
| G3 | `bin/run-tests-linux` never exercised | skipped by decision — CI's Linux job is the check |
| U1 | US3 scenario 6 needs an X3 | closed — scenario annotated: parity-only, disclosed in release notes |
| C1 | VII parallel-work check unrecorded | closed — notes in R2, R5, R9 and the plan's VII row |
| I3 | `release.yml` edit placed in the merge by plan/R1, in T047 by tasks | closed — R1 row and plan commit #1 point at T047 |
