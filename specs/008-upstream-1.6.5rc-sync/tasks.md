---
description: "Task list for the upstream 1.6.5rc sync"
---

# Tasks: Upstream 1.6.5rc Sync

**Input**: Design documents from `specs/008-upstream-1.6.5rc-sync/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md) (R1 is the per-file conflict table), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required (Constitution V). New code in US3 is written test-first; each new test is seen failing against the mutation named in [contracts/fb2-load-metadata.md](contracts/fb2-load-metadata.md) or the task, then the mutation is reverted. US1 and US2 are proven by the *existing* program (3,349 fork tests + 132 upstream), not by new tests.

**Story order**: US1 (the merge) is the foundation — nothing else can start before it. US2 (keep) is an audit of the merged tree and can overlap US3–US5. US3 (apply) is the only new production code. US4 and US5 are small and independent.

**Commits**: the merge commit closes US1 (plain `git commit`, not the speckit script — it is a merge). Every later phase ends in one semantic commit through `.specify/scripts/bash/speckit-commit.sh implement -m "<type>: <subject>" <files>`. This list supersedes the plan's commit list in one place: the `release.yml` relaxation becomes its own `ci:` commit (T047) instead of living inside the merge, so the deliberate deviation from upstream is visible in history for the next sync.

## Format: `[ID] [P?] [Story] Description`

---

## Phase 1: Setup (measure before anything changes)

- [ ] T001 Device "before" figures (human, `1.6.0-bb.5` still on the X4, the 944-FB2 card): run quickstart step 0 once — free heap at Home and with an FB2 open, the 24-row FB2 chapter-list window read (ms), first open of the 677-chapter reference book (s). Record them in `specs/008-upstream-1.6.5rc-sync/research.md` as a new **R15 "Device figures"** table (before column). SC-006 cannot be met later if this is skipped.
- [ ] T002 [P] Confirm the target and the toolchain: `git fetch upstream --tags`; `git rev-parse 1.6.5rc` = `a1ceb633`; `pio pkg install -e default` after the merge will pull pioarduino 55.03.311 — check `pio system info` and the venv per memory *platformio-install-quirks* so T033 does not stall on the platform download.
- [ ] T003 Start the merge on `sync/upstream-1.6.5rc`: `git merge --no-ff --no-commit 1.6.5rc`. Expect exactly 42 conflicted paths (`git diff --name-only --diff-filter=U | wc -l`), one `UD` (`src/activities/home/RecentBooksActivity.cpp`) and six `AA` (`test/content_opf_parser/*`). Any other count means upstream or the fork moved since 2026-09-27 — stop and re-run research R1 before resolving.

---

## Phase 2: User Story 1 — A fork user gets everything 1.6.5rc has (Priority: P1) 🎯 MVP

**Goal**: one merge commit with `1.6.5rc` as an ancestor, all 42 files resolved per research R1, the test program reconciled per R12, five firmwares building, host program green plain and sanitised.

**Independent Test**: `git merge-base --is-ancestor 1.6.5rc HEAD`; `bin/run-tests` and `bin/run-tests --asan` 100% with 69 suites; `pio run` for the five environments; inventory A walked on the device (quickstart steps 1–3, 6).

### Conflict resolution (uncommitted working tree; each task lists its files and the R1 rule)

- [ ] T004 [P] [US1] Build and release configuration: `platformio.ini` → `version = 1.6.5rc-bb.1`, everything else upstream's (verify `grep -c '^\[env:x4c-gh_release_rc\]'` = 1); `.github/workflows/release.yml` → upstream **verbatim** (the fork's edit comes in T044); `ROADMAP.md` → upstream verbatim.
- [ ] T005 [P] [US1] Documents: `docs/file-formats.md` → keep both sides (upstream's CLX1 section and "Version 46" note; the fork's TextBlock validation limits, `css_rules.cache`, FB2 and TXT sections); `USER_GUIDE.md` → upstream's Library section with `.fb2` *(fork-only)* inserted into its list of indexed formats, upstream's "Use book metadata" / "Rebuild library index" / "Check for updates" entries.
- [ ] T006 [P] [US1] Libraries, part 1: `lib/EpdFont/SdCardFont.cpp` → upstream's `accumulate` condition and `readOrder.reset()`, but `platform::freeHeap()` / `platform::millis()` and the `<PlatformSeam.h>` include stay; `lib/Epub/Epub/Page.cpp` → the fork's `MAX_PAGE_IMAGE_EDGE` guard block followed by upstream's `auto image = makeUniqueNoThrow<PageImage>(...)` / `return image;`, upstream's OOM log text; `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp` → the fork's borrowed `const char*` `classAttr/styleAttr/dirAttr` plus upstream's `bool hasHiddenAttr = false;` and the hidden-attribute handling that follows in the auto-merged region; `lib/FsHelpers/FsHelpers.cpp` → both `truncateAtInvalidUtf8` (anonymous namespace) and `isSafePathComponent`.
- [ ] T007 [P] [US1] Libraries, part 2: `lib/GfxRenderer/Bitmap.cpp` and `lib/GfxRenderer/BitmapHelpers.h` → upstream (`isValid()`, `originalThresholds`, `BmpReaderError::OomDitherer`; delete the fork's `valid()`/`allocated()`); `lib/hal/HalStorage.cpp` → upstream's block (adds `modificationTime()`) with the fork's `if (impl == nullptr) return;` guards re-applied to `flush()` and `rewindDirectory()`; `lib/KOReaderSync/KOReaderSyncClient.cpp` → upstream's `HalMemory::getDefaultHeap()`; `lib/JsonParser/ReleaseJsonParser.h` → upstream (drop `alternateAssetName`); `lib/I18n/translations/english.yaml` → upstream's `STR_INVALID_FONT_MANIFEST` plus every other fork `STR_FONT_*` key (drop `STR_FONT_MANIFEST_INVALID` only).
- [ ] T008 [P] [US1] `lib/PngToBmpConverter/PngToBmpConverter.cpp` → upstream's `originalThresholds` arguments and `isValid()`/fail-on-OOM branches, but keep the fork's `makeUniqueNoThrow*` row buffers (delete upstream's `free(...)`/`delete[]` chains — nothing to free) and keep `#include <PlatformSeam.h>`; replace upstream's `vTaskDelay(1)` in `yieldDuringDecode` with `platform::taskDelay(1)`. Add `void taskDelay(uint32_t ticks);` to `lib/Platform/PlatformSeam.h` and implement it in `lib/Platform/PlatformSeam.cpp` (device: `vTaskDelay`; host: no-op), following the file's existing `millis()` pattern.
- [ ] T009 [P] [US1] Network: `src/network/CrossPointWebServer.cpp` → keep every `WebPathUtils::` call, the `pathHasProtectedComponent` 403 checks and the `new (std::nothrow) WebDAVHandler()` block; take upstream's 7-entry `collectedHeaders` (adds `If-None-Match`), its ETag/Cache-Control static-page handlers and escaped files page; drop upstream's inline `normalizeWebPath`/`isProtectedItemName` and its inline WS `START` parsing (the fork's `parseWsStart` stays — confirm in `src/network/WebPathUtils.cpp` that `checkItemName` rejects `/`, `\`, `.`, `..` and empty, which is what upstream's `isSafePathComponent` rejects; add the missing case if any). `src/network/WebDAVHandler.{cpp,h}` → fork (no `getDepth/getOverwrite/getMimeType`; `WebPathUtils::mimeTypeForPath` already serves). `src/network/OtaUpdater.cpp` → upstream's single `crosspoint-<tag>-<device>.bin` flow with the fork's heap-allocated `parserHolder` kept (delete `taggedAssetName`/`setAlternateFirmwareAssetName`).
- [ ] T010 [P] [US1] Activities, part 1: `src/activities/ActivityManager.cpp` → upstream `goToLibrary()`; `src/activities/home/RecentBooksActivity.cpp` → `git rm`; `src/activities/home/HomeActivity.h` → fork's `#include "./HomeMenuMap.h"`, drop upstream's in-class `menuItemToIndex`/`indexToMenuItem`; `src/activities/home/HomeMenuMap.h` → rename `RECENTS` → `LIBRARY` (both functions, and the comment); `src/activities/home/FileBrowserActivity.cpp` → `makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, utf8ComposeNfc(entry))` with upstream's comment; `src/SettingsList.h` → keep `buildBaseSettingsList()` and add upstream's `SettingInfo::Toggle(StrId::STR_LIBRARY_USE_METADATA, &CrossPointSettings::libraryUseMetadata, "libraryUseMetadata", StrId::STR_CAT_SYSTEM)` inside it, after `STR_SHOW_HIDDEN_FILES`.
- [ ] T011 [P] [US1] Activities, part 2: `src/activities/reader/EpubReaderActivity.cpp` → upstream's two hunks (`ChapterPosition`, new `KOReaderSyncActivity` constructor) with `makeUniqueNoThrow` in place of `std::make_unique`; `src/activities/reader/KOReaderSyncActivity.cpp` → upstream (all four hunks); `src/activities/reader/TxtReaderActivity.h` → upstream (`GfxRenderer&`); `src/activities/reader/XtcReaderChapterSelectionActivity.cpp` → fork (`xtc_reader::findChapterIndexForPage`); `src/components/themes/BaseTheme.{h,cpp}` → `static void drawStatusBar(..., const char* title, ...)` and `static void drawHelpText(...)` (upstream's `static`, the fork's borrowed title); `src/activities/boot_sleep/SleepActivity.cpp` → upstream's `originalThresholds` arguments in the EPUB branch, but on `generateCoverBmp` failure only `LOG_ERR` and fall through (no `return (this->*renderNoCoverSleepScreen)()`), so the stub card at the end of the function still runs.
- [ ] T012 [US1] Font manifest (`src/activities/settings/FontDownloadActivity.{cpp,h}`, `src/util/FontManifest.{h,cpp}`): adopt upstream's data model — `StrRef`, `ManifestFile{name,size,crc32}`, `ManifestFamily{name,description,fileStart,fileCount,totalSize,scriptMask,installed,hasUpdate}`, `MAX_SCRIPT_GROUPS = 32`, one `stringArena_` and one `files_` array sized in a first pass — but host the parse in the fork's seam: `FontManifest.h` declares the structs, a `FontManifestArena` (arena + files + families + script-group labels) and `FontManifestError parseFontManifest(JsonDocument&, std::string& baseUrl, FontManifestArena&)`; `FontDownloadActivity` owns one `FontManifestArena` and calls it. Keep the fork's `setFormattedError(tr(STR_FONT_*_FORMAT), ...)` error paths and `tr(STR_INVALID_FONT_MANIFEST)`. Mirror upstream's bounds exactly (more than 32 groups → extra ignored with `LOG_ERR`; missing/invalid `crc32` → invalid manifest).
- [ ] T013 [US1] Test reconciliation, registry: `test/CMakeLists.txt` → the fork's `crosspoint_suite()` list plus `library_text`, `library_format`, `library_index_file`, `library_builder`, `koreader_xpath_resolver`, `progress_comparison`, `chapter_position`, `inflate_stream`, `fs_helpers`, `absolute_grayscale`, `sd_card_font`; remove `crosspoint_suite(sdcard_font)`. `test/chapter_html_slim_parser/CMakeLists.txt` → both source lists (`test/support/AllocCounter.cpp` + `lib/Epub/Epub/Page.cpp`, `blocks/TextBlock.cpp`, `lib/MiniBidi/BidiUtils.cpp`, `lib/MiniBidi/minibidi.c`); in `test/chapter_html_slim_parser/ParserLinkStubs.cpp` delete the `TextBlock::TextBlock`, `TextBlock::hasRuby`, `PageLine::*`, `PageImage::*`, `PageHorizontalRule::*`, `startsWithRtl`, `computeVisualWordOrder` stubs the real sources now define (keep the hyphen/Hyphenator/ImageDecoder stubs).
- [ ] T014 [P] [US1] `test/sd_card_font/` absorbs `test/sdcard_font/`: move the fork's `TEST(...)` cases from `test/sdcard_font/SdCardFontTest.cpp` into `test/sd_card_font/SdCardFontTest.cpp`, reconcile the two `stubs/HalStorage.h`/`stubs/Logging.h` into one pair that satisfies both sets, then `git rm -r test/sdcard_font`. One target `SdCardFontTest`; no case dropped.
- [ ] T015 [P] [US1] `test/content_opf_parser/`: resolve the six `AA` files to the fork's versions, then append upstream's five `TEST(ContentOpfParserMetadata, ...)` cases from `1.6.5rc:test/content_opf_parser/ContentOpfParserTest.cpp` and add upstream's `stubs/Serialization.h` and `stubs/FsHelpers.h` (`git show 1.6.5rc:test/content_opf_parser/stubs/...`); extend the fork's `stubs/Epub.h`/`stubs/Epub/BookMetadataCache.h` only where those five tests need it (the `metadataOnly` parse path). `CMakeLists.txt` stays the fork's plus any source upstream's cases link.
- [ ] T016 [P] [US1] OTA tests re-target the single asset layout: `test/ota_asset_selection/{OtaAssetSelectionTest,X4VariantTest,StickyVariantTest,PapermonoVariantTest}.cpp` and `test/release_json_parser/ReleaseJsonParserTest.cpp` — every `firmware.bin` / `firmware-<board>.bin` expectation becomes `crosspoint-<tag>-<device>.bin` (`x3-x4` for the C3), cases asserting the alternate/fallback name are rewritten to assert the tag-derived name is set once `foundTag()` fires; merge upstream's 52 added lines of `ReleaseJsonParserTest.cpp` (they pin the same behaviour). Each rewritten case names the upstream change (#3493) in a one-line comment.
- [ ] T017 [P] [US1] `test/font_system/FontManifestTest.cpp` (26 cases) re-targets the arena API from T012: same inputs, same rejections (too many groups, missing crc32, malformed group, oversize), assertions read strings through the arena. No case dropped.
- [ ] T018 [P] [US1] `test/library_helpers/LibraryHelpersTest.cpp` → `HomeMenuItem::RECENTS` → `HomeMenuItem::LIBRARY` in the `HomeMenuMap` cases (`:584-`); `test/library_helpers/stubs/activities/ActivityManager.h` → rename the enumerator the same way.
- [ ] T019 [P] [US1] KOSync: `git rm lib/KOReaderSync/SmartSyncDecision.{h,cpp}`; remove the `SmartSync` cases and their include from `test/kosync_client/KosyncClientTest.cpp` and `CMakeLists.txt` (research R11 — replaced by upstream's `progress_comparison`); add `test/kosync_client/stubs/HalMemory.h` returning a configurable `{freeBytes, largestBlockBytes}` so the TLS-heap check stays covered (port the fork's low-heap case from `platform::setHeap()` to the stub).
- [ ] T020 [P] [US1] Pin flips: for each test in `test/chapter_xpath_resolver/ChapterXPathResolverTest.cpp` and `test/progress_mapper/ProgressMapperTest.cpp` marked "documents current limitation", run it against the merged `lib/KOReaderSync/ChapterXPathResolver.cpp` (#3174): where the upstream-expected value named in the comment now holds, flip the assertion to it and drop the limitation note; where it does not, leave the pin. Record the list of flips in the merge commit's body (T025).
- [ ] T021 [US1] `git add -A` the resolved paths, then `git diff --check` and `./bin/clang-format-fix -g`; `git status` must show no `U`/`AA`/`UD` entries and no stray `.orig` files; `grep -c CROSSPOINT_SANITIZE .github/workflows/ci.yml` = 1 (the fork's sanitised unit-test matrix entry survived the auto-merge, FR-005).

### Build and prove

- [ ] T022 [US1] `bin/run-tests`: 100% pass, 69 suites, 5 by-design skips only. Fix compile fallout in stubs (`HalMemory`, `taskDelay`, `Serialization.h`) until green; record the plain total.
- [ ] T023 [US1] `bin/run-tests --asan`: same totals, sanitizer-clean (macOS: no `detect_leaks`). Upstream suites that report under ASan are fixed in the test (never skipped) — if a production defect surfaces, note it for a separate fix commit.
- [ ] T024 [US1] `pio run -e default`, then `-e sticky`, `-e x4pro`, `-e x4c`, `-e papermono`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. Record `default`'s RAM/Flash percentages next to `1.6.0-bb.5`'s in research R15.
- [ ] T025 [US1] Commit the merge: `git commit` with the default `Merge tag '1.6.5rc' into sync/upstream-1.6.5rc` subject and a body listing the R1 resolution classes and the R12 test moves. Verify `git merge-base --is-ancestor 1.6.5rc HEAD`.
- [ ] T026 [US1] Device walk of inventory A (human, flash `default` on the X4). First, FR-006 on the card's existing caches: open an EPUB and an FB2 last read on `1.6.0-bb.5` — the EPUB re-lays out its chapter (v46), the FB2 opens straight from its cache with no indexing popup, and the serial log shows no cache rejection for the FB2. Then: Home shows Library; Library builds its index and shows the four tabs; an EPUB with an ordered list is numbered; the reader menu shows the chapter page after returning from the chapter list; an idle-then-short-press registers; a white-background sleep cover shows the background as transparent. Tick each in this task's line as verified; anything wrong is a US1 defect.

**Checkpoint**: the upgrade exists and is green. US2–US5 may start.

---

## Phase 3: User Story 2 — Nothing the fork added is lost (Priority: P1)

**Goal**: an audit that every inventory-B feature and every FR-004 guarantee survived the merge, proven by the existing program and the device.

**Independent Test**: the fork's suites all present and passing (T022/T023 already); quickstart checks of specs/002–007 pass on the reference books; the four FR-004 guarantees hold by inspection and test.

- [ ] T027 [P] [US2] Guarantee audit by grep, recorded as a checklist in research R15: (a) `grep -n pathHasProtectedComponent src/network/CrossPointWebServer.cpp` covers list, create, delete and WS upload paths; (b) `grep -rn 'new [A-Z]' src lib --include=*.cpp | grep -v nothrow` is empty outside vendored code; (c) `grep -n MAX_PAGE_IMAGE_EDGE lib/Epub/Epub/Page.cpp` precedes `makeUniqueNoThrow<PageImage>`; (d) `MAX_SCRIPT_GROUPS` and the crc32 check exist in `src/util/FontManifest.cpp`, and `ReleaseJsonParser` still rejects assets whose size overflows `size_t` (its fork test passes).
- [ ] T028 [P] [US2] Suite-presence audit: `bin/run-tests --filter 'fb2|xtc_fb2|reader_helpers|alloc_guards|zip_file|book_metadata_cache|epub_section|txt_reader|serialization|png_decode|jpeg_to_bmp|web_dav_paths|network_helpers|persistable_stores|font_system|settings_input|library_helpers|platform_helpers|text_block_layout'` — every suite in data-model.md's "Fork feature inventory" table runs and passes.
- [ ] T029 [US2] Device: specs/003–007 quickstart checks on the reference books (human): *Марсианские хроники* lists 66 chapters at their depths and opens on its cover; a >256-section book lists and opens its last chapter; crossing into an unread chapter after a normal dwell shows no indexing popup; the worst front-matter book's percentage moves through its front matter; a coverless book sleeps to the stub card with wrapped title and author.
- [ ] T030 [US2] Corpus under the sanitiser: `bin/run-tests --asan --filter 'corpus|fb2|zip|json|css|png|jpeg|xtc'` — every malformed file rejected deterministically, no sanitizer output (this is T023 narrowed; it exists so a US2 regression is attributable).

**Checkpoint**: inventory B confirmed. No commit unless T027–T030 found a defect (then one `fix:` commit per defect).

---

## Phase 4: User Story 3 — Upstream's enhancements reach the fork's own features (Priority: P2)

**Goal**: FB2 books in the Library with metadata; the FB2 reader's three chrome fixes; FB2 covers with the new thresholds and the stub fallback; inherited fixes verified.

**Independent Test**: on the device card the Library's Title tab lists every `.fb2`, each opens into the FB2 reader, metadata toggles the row text; `bin/run-tests --filter 'library|fb2'` green with the new cases.

### 4a. FB2 in the Library (contract: [fb2-load-metadata.md](contracts/fb2-load-metadata.md))

- [ ] T031 [P] [US3] Test first — `test/library_builder/stubs/Fb2.h`: mirror `stubs/Epub.h` (a `FakeMetadata` map keyed by path, `loadMetadata(title, author)` that increments `fake::parses` and honours `success`). Add to `test/library_builder/LibraryBuilderTest.cpp`: (1) a card with `a.fb2` beside an `.epub` indexes both and `stats.books == 2`; (2) with `readMetadata = true` the `.fb2` row's title/author come from the stub and `stats.parsed` counts it once, its record `metadataStatus == CLIX_METADATA_EXTRACTED`; (3) with `success = false` the row's title is the filename stem and status is `CLIX_METADATA_FAILED`. Add `stubs/Fb2.h`'s include path to the suite's `CMakeLists.txt`. Mutations: (1) fails when `.fb2` is missing from `isBookName`; (2) when `extractionExpected` ignores FB2; (3) when the failure branch is swapped.
- [ ] T032 [P] [US3] Test first — `test/fb2_metadata_parser/Fb2MetadataParserTest.cpp`: `MetadataOnlyParseStopsBeforeTheBody` — construct the parser with a null sink and the metadata-only flag on a fixture with sections; `parse()` returns true, title and author are correct, the fixture's `<section` byte offset is greater than the bytes the parser consumed (expose a `bytesConsumed()` counter or count `file.read` calls through the stub), and a sink that fails if called is never called. Mutation: remove the `XML_StopParser` call in `endElement("title-info")`.
- [ ] T033 [P] [US3] Test first — `test/fb2_book/Fb2BookTest.cpp`: `LoadMetadataReadsTheCacheHeaderWithoutBuilding` — after a `load(true)` writes `book.bin`, a fresh `Fb2` object's `loadMetadata(t, a)` returns the same title/author with `openForReadCount()` incremented by exactly one (the cache) and no `sections/` directory created; `LoadMetadataWithoutCacheParsesAndWritesNothing` — on an uncached book it returns true, and afterwards `book.bin` does not exist. Mutation: route `loadMetadata` through `load(true)`.
- [ ] T034 [US3] Implement `lib/Fb2/Fb2/Fb2MetadataParser.{h,cpp}`: a `bool metadataOnly` constructor argument (default false); when set, `endElement("title-info")` (`:199`) and `startElement("body")` call `XML_StopParser(parser, XML_FALSE)` and set `metadataDone = true`; in `parse()` treat `XML_STATUS_ERROR` with `XML_GetErrorCode() == XML_ERROR_ABORTED && metadataDone` as success and skip the whole-file-fallback block; the sink is never touched in this mode.
- [ ] T035 [US3] Implement `Fb2::loadMetadata(std::string& title, std::string& author)` in `lib/Fb2/Fb2.{h,cpp}` per the contract: clear both; if `loadMetadataCache()` succeeds copy `this->title/author`; else run `Fb2MetadataParser(filepath, /*sink=*/{}, /*metadataOnly=*/true)`, on success take its title/author; apply `utf8ComposeNfc` to both (include `<Utf8.h>`, as `Epub.cpp` does); never call `setupCacheDir()` or `buildMetadataCache()`. Then `lib/LibraryIndex/LibraryBuilder.cpp`: `isBookName()` adds `FsHelpers::checkFileExtension(name, ".fb2")`; `extractionExpected` becomes `st.readMetadata && (FsHelpers::hasEpubExtension(name) || FsHelpers::hasFb2Extension(name))`; the extraction block branches on `hasFb2Extension` to `Fb2(fullPath, CACHE_DIR).loadMetadata(bookTitle, author)` (include `<Fb2.h>`); everything after (status, fold, author key) is shared. Confirm T031–T033 pass and their mutations fail.
- [ ] T036 [US3] Commit `feat(library): index FictionBook 2 files` with T031–T035's files.

### 4b. FB2 reader parity

- [ ] T037 [P] [US3] Test first — `test/xtc_fb2_readers/` (the `Fb2ReaderMath` cases): `percentForPosition` — given a `ChapterPosition{pageIndex, totalPages}` and a chapter's `SectionInfo`, returns `calculateProgress(chapter, pos.chapterFraction()) * 100` rounded, clamped to `[0, 100]`, and `0` when `totalPages == 0` — `chapterFraction()` is upstream's 0-based `pageIndex / totalPages` (FR-024: the EPUB menu's rule), not the status bar's 1-based page. Mutation: drop the clamp (a page index past the estimated total must not exceed 100).
- [ ] T038 [US3] Implement in `src/activities/reader/Fb2ReaderMath.{h,cpp}` (`int percentForPosition(const Fb2& book, const Fb2::SectionInfo& chapter, const ChapterPosition& pos)`, include `"ChapterPosition.h"`) and in `src/activities/reader/Fb2ReaderActivity.{h,cpp}`: `ChapterPosition chapterPosition() const { return section ? ChapterPosition{section->currentPage, section->pageCount} : ChapterPosition{nextPageNumber, cachedSectionTotalPageCount}; }`; `openReaderMenu()` (`:146-152`) passes `position.displayPage()`, `position.totalPages` and `percentForPosition(...)` (the status bar keeps its own 1-based fraction, as upstream's does); `GO_TO_PERCENT` (`:220-224`) seeds `initialPercent` the same way. Keep `cachedSectionTotalPageCount` updated wherever `section` is released (it already is at `:180,195,214`; verify).
- [ ] T039 [US3] Commit `fix(fb2): keep the reader menu on the cached chapter position` (T037–T038 files).
- [ ] T040 [P] [US3] Test first — `test/fb2_section_cache/`: give the suite's `GfxRenderer` stub a `FontCacheManager` stub with a `releaseSdFontCaches()` call counter; `StartBuildReleasesSdFontCachesOnce` — a `Fb2Section::createSectionFile` build increments it exactly once before the first page is written. Mutation: delete the call.
- [ ] T041 [US3] Implement in `lib/Fb2/Fb2/Fb2Section.cpp` `startBuild(...)`: `if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();` with the same one-line comment `Section.cpp` carries (#3527); include `<FontCacheManager.h>`. Commit `fix(fb2): release SD font caches before laying out a chapter`.
- [ ] T042 [US3] `src/activities/reader/ReaderUtils.h` `displayBaseWithRefreshCycle`: keep the combined-base branch; replace the non-combined `displayWithRefreshCycle(...)` call with upstream's non-tiled sequence — `if (pagesUntilFullRefresh <= 1) { renderer.displayBuffer(HalDisplay::HALF_REFRESH); renderer.preconditionGrayscale(); pagesUntilFullRefresh = SETTINGS.getRefreshFrequency(); } else { renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH); pagesUntilFullRefresh--; }` with a two-line comment naming #3439 and that `preconditionGrayscale()` is a no-op on X4. Confirm `EpubReaderActivity.cpp`'s remaining caller (`:1585` at 1.6.5rc) and `TxtReaderActivity.cpp` still compile and that the EPUB tiled path is untouched. Add a row to `plan.md` Complexity Tracking: "X3 sequencing in the shared helper — hardware-bound, no host test; unverified without an X3". Commit `fix(reader): precondition grayscale on cleanup refreshes in the base-display helper`.

### 4c. Sleep cover parity

- [ ] T043 [P] [US3] Test first — `test/fb2_cover_extractor/`: the suite's `JpegToBmpConverter` stub records the `originalThresholds` argument; `ExtractForwardsThresholdFlag` — `Fb2CoverExtractor::extract(true)` reaches the converter with `true`, `extract(false)` with `false`; in `test/fb2_book/`, `CoverBmpPathNamesTheThresholdVariant` — `getCoverBmpPath(true)` ends in `cover_original.bmp`, `getCoverBmpPath(false)` in `cover_legacy_v2.bmp`. Mutation: ignore the flag in `extract`.
- [ ] T044 [US3] Implement: `lib/Fb2/Fb2.{h,cpp}` — `std::string getCoverBmpPath(bool originalThresholds = false) const` (`cachePath + "/cover" + (originalThresholds ? "_original" : "_legacy_v2") + ".bmp"`) and `bool generateCoverBmp(bool originalThresholds = false) const` passing it to `Fb2CoverExtractor::extract(originalThresholds)`; `lib/Fb2/Fb2/Fb2CoverExtractor.{h,cpp}` — `extract(bool originalThresholds)` forwards to `JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp, /*crop=*/true, originalThresholds)` (`:231`; thumbs unchanged); `src/activities/boot_sleep/SleepActivity.cpp` FB2 branch — `lastFb2.generateCoverBmp(originalThresholds)` / `getCoverBmpPath(originalThresholds)`, and confirm (T011) that failure in every branch falls through to `renderCoverStubSleepScreen`. Commit `fix(sleep): render FB2 covers with the panel thresholds`.

### 4d. Verify what is inherited (FR-027, research R7)

- [ ] T045 [US3] Device (human): quickstart step 6 — Title tab count equals the card's `.fb2` count; open one FB2 from each tab; Recent still holds the pre-flash FB2 entries; hold-to-remove in Recent; delete from Browse Files clears `fb2_<hash>`; an NFD-named Korean FB2 renders composed; holding Down through a chapter-list repaint drops no press; after 30 s idle in the FB2 reader one short press turns the page (#3463 on an FB2 flow, FR-027). Quickstart step 7 — menu chapter page after the chapter list; end-of-book selection; FB2 cover sleep and coverless stub. Record outcomes in research R15.
- [ ] T046 [US3] Device (human): Library index build time on the card with **Use book metadata** off, then a rebuild with it on, plus the indexed book count — record in research R15 (SC-005).

**Checkpoint**: every "apply" item landed as its own commit; device confirmations recorded.

---

## Phase 5: User Story 4 — Fork releases ride upstream's release pipeline (Priority: P2)

**Goal**: one deliberate edit to upstream's workflow so `1.6.5rc-bb.1` pre-releases get their five assets ([contract](contracts/release-pipeline.md)).

**Independent Test**: the published pre-release carries exactly five `crosspoint-1.6.5rc-bb.1-<device>.bin`, none uploaded by hand (Phase 7).

- [ ] T047 [US4] `.github/workflows/release.yml` "Validate release version" step: replace the `if prerelease … ${configured_version}rc … else …` block with the single `test "$version" = "$configured_version"` and the two-line comment from the contract. Confirm `platformio.ini` still reads `version = 1.6.5rc-bb.1`. Commit `ci: accept fork release tags in the version check`.
- [ ] T048 [P] [US4] Dry-check the contract's other rows with `strings` on the local `default` build: `strings .pio/build/default/firmware.bin | grep -o '1\.6\.5rc-bb\.1[^ ]*'` — the development stamp contains the version (the RC envs' `-rc+<sha>` form is CI-only). Record in research R15.

**Checkpoint**: pipeline ready; exercised in Phase 7.

---

## Phase 6: User Story 5 — Documentation and governance converge (Priority: P3)

**Goal**: every stated fact matches the merged firmware (research R14).

**Independent Test**: quickstart step 5's three greps.

- [ ] T049 [P] [US5] `AGENTS.md` (stage as `AGENTS.md`): `.crosspoint/` list (`:169`) gains `library.idx` and a one-clause description; cache table (`:1080`) `SECTION_FILE_VERSION` **45** → **46**; the bump example (`:1101`) "Was 45, now 46" → "Was 46, now 47"; Directory Structure gains `lib/LibraryIndex/` (CLX1 index) and `src/activities/library/`; HAL table gains `HalMemory` (heap capability queries, free functions); any "Recent Books" screen mention → Library; platform line mentions pioarduino 55.03.311 if a version is stated. Check `USER_GUIDE.md`'s `.fb2` *(fork-only)* mention survived T005.
- [ ] T050 [P] [US5] `specs/001-crosspoint-reader-baseline/spec.md`: add a one-line *"Superseded by the Library (specs/008-upstream-1.6.5rc-sync) — the Recent Books screen no longer exists; the recent list survives as the Library's Recent tab."* note under FR-029, FR-037, FR-038, US2 acceptance scenario 5 (`:52`) and the `RecentBook / RecentBooksStore` entity (`:597`). Do not rewrite the requirements.
- [ ] T051 [US5] SC-008 check: `grep -n 'Recent Books' USER_GUIDE.md README.md AGENTS.md` shows no top-level screen reference and `grep -n SECTION_FILE_VERSION AGENTS.md lib/Epub/Epub/Section.cpp` agree on 46. Commit `docs: converge the guides and the baseline spec with the merged firmware` (T049–T050 files).

---

## Phase 7: Polish — gates, landing, release

- [ ] T052 Full gates in order: `./bin/clang-format-fix -c`; `bin/run-tests`; `bin/run-tests --asan`; `pio run -e default` (+ the four S3 environments); `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. Record the final test/suite totals in research R15 (SC-002 floor: every surviving fork test + 132).
- [ ] T053 [P] `bin/run-simulator`: Library (four tabs, collapse, search), FB2 from each tab, FB2 menu after the chapter list, coverless FB2 sleep → stub; repeat Library and FB2 reader in all four orientations (gate 6). Note anything visual in research R15.
- [ ] T054 Device "after" figures (human): quickstart step 8 on the final build — the four SC-006 figures once each, into R15's after column; attribute any worse figure to a named upstream change or open a `fix:` before release.
- [ ] T055 SC-009 review first: `git diff --stat <merge-commit>..HEAD` lists only files in the Apply areas (Library ↔ FB2, FB2 reader, sleep cover, `release.yml`, tests) and docs — anything else is explained or moved. Then, with the user's explicit go-ahead only: push `sync/upstream-1.6.5rc` to `origin`, open the PR against `master` with AI usage disclosed per the template, wait for `Test Status`, and land it with `gh pr merge --merge --delete-branch` (never `--squash`, FR-002). Verify on `master`: `git merge-base --is-ancestor 1.6.5rc origin/master`.
- [ ] T056 Release (human): tag `1.6.5rc-bb.1` on the merged `master`, publish it as a **pre-release**; confirm the workflow attached exactly five `crosspoint-1.6.5rc-bb.1-<device>.bin` with zero manual uploads and `strings` each for the tag. Release notes per the fork convention: R15's before/after figures against `1.6.0-bb.5`; behaviour changes — Library replaces Recent Books, EPUB chapters re-laid out once, RC basis, X3 anti-aliasing change unverified on hardware, an index built by stock upstream needs **Rebuild library index** to show FB2 books.

---

## Dependencies & Execution Order

- **Phase 1 → Phase 2**: T001 must precede any flashing; T003 precedes all resolution tasks.
- **Phase 2 (US1)**: T004–T011 and T013–T020 touch disjoint files and may run in parallel; T012 precedes T017 (arena API); T013 precedes T022; T021 → T022 → T023 → T024 → T025 → T026 are sequential.
- **Phase 3 (US2)**: after T025; T027/T028 parallel; T029/T030 need the device / the ASan build.
- **Phase 4 (US3)**: after T025. 4a, 4b, 4c are independent of each other; within each, tests precede implementation; T042 needs no test (hardware-bound, justified in plan). T045/T046 need T036–T044 flashed.
- **Phase 5 (US4)**: after T025; independent of US3.
- **Phase 6 (US5)**: after T025 (facts to document exist); T049/T050 parallel.
- **Phase 7**: T052–T054 after every earlier commit; T055 after T052–T054; T056 after T055.

## Parallel Examples

```text
# After T003 (merge started), one agent per group:
T004 build/CI/docs config   T005 documents   T006 libs-1   T007 libs-2   T008 PNG + seam
T009 network                T010 activities-1   T011 activities-2   T012 font manifest (then T017)
T013 registry + slim parser T014 sd_card_font   T015 content_opf_parser   T016 OTA tests
T018 library_helpers        T019 KOSync         T020 pin flips

# After T025 (merge committed):
US3 4a tests (T031, T032, T033)  |  US3 4b test (T037)  |  US3 4c test (T043)  |  US4 T047  |  US5 T049, T050
```

## Implementation Strategy

1. **MVP = Phase 1 + Phase 2 (US1)**: after T025 the fork *is* on 1.6.5rc and green; T026 proves it on the device. Everything a stock-upstream user has, a fork user has — except FB2 in the Library.
2. **US2 audit** can run while US3 is written; it produces no commits unless it finds a defect.
3. **US3 in three independent slices** (Library, reader, sleep), each its own commit, each with its tests first.
4. **US4 and US5** are one commit each and can be done any time after the merge.
5. **Phase 7** lands the PR as a merge commit and cuts `1.6.5rc-bb.1` as a pre-release; the release notes carry R15.

## Notes

- Every `new` written in this feature is `makeUniqueNoThrow`/`new (std::nothrow)`; every new buffer is on the heap or under 256 B (Principle II).
- Tests are never deleted to make a run green; the only deletions are `SmartSyncDecision`'s cases with the code they exercised (R11) and stubs replaced by real sources (T013), both named in their commits.
- The scratch merge worktree is gone; T003 works in the real tree on the sync branch. If the merge must be abandoned: `git merge --abort` leaves `master` and the branch untouched.
