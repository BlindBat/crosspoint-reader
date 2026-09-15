---

description: "Retrospective task list for the CrossPoint Reader firmware baseline"
---

# Tasks: CrossPoint Reader Firmware (Retrospective Baseline)

**Input**: Design documents from `/specs/001-crosspoint-reader-baseline/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: PROJECT OVERRIDE (Constitution Principle V, NON-NEGOTIABLE): host gtest coverage is required for host-reachable logic. This retrospective list records the suites that exist; suites that are still missing are appended by `/speckit-converge` as convergence tasks.

**Organization**: Tasks are grouped by user story. Every task below is marked complete because it describes code that ships on fork `master` today; the file paths are where that work lives. Convergence phases at the bottom hold the remaining work.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- Single embedded project: firmware under `src/` and `lib/`, host tests under `test/`, tooling under `bin/` and `scripts/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build environments, generators and quality tooling

- [X] T001 Define PlatformIO environments per board (default/gh_release/gh_release_rc/slim for C3 X4+X3; sticky*, x4pro*, x4c*, papermono*) with base flags, wolfSSL memory tuning and the `firmware_tuned` heap-reclamation core rebuild in `platformio.ini`
- [X] T002 [P] Define the 16 MB flash layout with two 0x640000 OTA app slots and otadata in `partitions.csv`
- [X] T003 [P] Pre-build scripts: gzip web pages into PROGMEM (`scripts/build_html.py`), generate i18n tables (`scripts/gen_i18n.py`), inject the dev version string (`scripts/git_branch.py`), patch wolfSSL/JPEGDEC/pioarduino cache (`scripts/patch_wolfssl.py`, `scripts/patch_jpegdec.py`, `scripts/patch_pioarduino_cache.py`)
- [X] T004 [P] Formatting and static-analysis tooling: `bin/clang-format-fix` (clang-format 21), cppcheck config in `platformio.ini`, opt-in hooks `bin/install-hooks`, `.githooks/pre-commit`, `.githooks/pre-push`
- [X] T005 [P] CI workflows: format, cppcheck, four-board build with OTA-named artifacts, unit tests, aggregated status, releases, release candidates, font packs, semantic PR titles in `.github/workflows/*.yml`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: HAL, memory, storage, settings, activity framework, renderer and input that every story depends on

- [X] T006 Serialize all SD access behind a recursive mutex with `HalFile` handles, USB-MSC hand-off and deep-sleep unmount in `lib/hal/HalStorage.{h,cpp}`
- [X] T007 [P] Wrap the SDK display with refresh modes, async refresh, grayscale plane helpers and framebuffer lending in `lib/hal/HalDisplay.{h,cpp}`
- [X] T008 [P] Wrap buttons, touch, wake-reason classification and USB state in `lib/hal/HalGPIO.{h,cpp}`; CPU scaling, battery polling and deep-sleep hardware teardown in `lib/hal/HalPowerManager.{h,cpp}`
- [X] T009 [P] Panic capture via linker wraps, crash report writer and reset-reason classification in `lib/hal/HalSystem.{h,cpp}`; levelled logging with an RTC log ring in `lib/Logging/Logging.{h,cpp}`
- [X] T010 [P] Clock, frontlight and tilt-sensor HALs in `lib/hal/HalClock.*`, `lib/hal/HalFrontlight.*`, `lib/hal/HalTiltSensor.*`
- [X] T011 [P] Nothrow allocation helpers and framebuffer scratch lending in `lib/Memory/Memory.h`, `lib/Memory/BuildScratch.{h,cpp}`
- [X] T012 [P] Binary serialization with bounded string reads and buffered file I/O in `lib/Serialization/Serialization.h`, `lib/Serialization/BufferedFile.h`
- [X] T013 JSON store base with a single ArduinoJson instantiation, store mutex, resave protocol, password obfuscation and CRC integrity in `lib/Serialization/PersistableStore.{h,cpp}`, `lib/Serialization/ObfuscationUtils.{h,cpp}`, `lib/Serialization/CredentialIntegrity.h`
- [X] T014 Settings singleton with migrations, clamping, `StatusBarSpec` and `ReaderRenderSpec` in `src/CrossPointSettings.{h,cpp}`; runtime state in `src/CrossPointState.{h,cpp}`
- [X] T015 Single settings table driving the device UI, JSON persistence and the web API in `src/SettingsList.h`
- [X] T016 Activity lifecycle, activity stack with typed results, shared render task and render lock in `src/activities/Activity.*`, `src/activities/ActivityManager.*`, `src/activities/ActivityResult.h`, `src/activities/RenderLock.h`
- [X] T017 Logical button mapping, long-press policy and touch gesture vocabulary in `src/MappedInputManager.{h,cpp}`, `src/util/ButtonNavigator.{h,cpp}`
- [X] T018 Orientation-aware renderer with text drawing, kerning, combining marks, bitmaps, region snapshots and strip grayscale in `lib/GfxRenderer/GfxRenderer.{h,cpp}`, `lib/GfxRenderer/Bitmap.*`, `lib/GfxRenderer/BitmapHelpers.*`
- [X] T019 [P] Built-in compressed fonts, glyph decompressor and page prewarm scan in `lib/EpdFont/EpdFont*.{h,cpp}`, `lib/EpdFont/FontDecompressor.{h,cpp}`, `lib/GfxRenderer/FontCacheManager.{h,cpp}`, `src/fontIds.h`, `src/main.cpp` (font registration)
- [X] T020 [P] UTF-8 decoding/NFC composition and UAX#9 bidi with Arabic shaping in `lib/Utf8/*`, `lib/MiniBidi/*`
- [X] T021 Theme system (Classic, Lyra, Lyra Extended, RoundedRaff), FreeInkUI hosting, list/tab bases, popups and slider dialogs in `src/components/**`, `src/activities/UiListActivity.*`, `src/activities/UiTabListActivity.*`
- [X] T022 [P] Translation catalogs (34 YAML files) and the runtime lookup in `lib/I18n/translations/*.yaml`, `lib/I18n/I18n.{h,cpp}`
- [X] T023 [P] File-system helpers (natural sort, extension detection, path normalisation, FAT-safe names) in `lib/FsHelpers/*`, `src/util/StringUtils.*`
- [X] T024 Main loop: boot classification, SD mount, crash check, resume routing, power-button policy, auto-sleep, screenshots combo, serial commands in `src/main.cpp`, `src/SilentRestart.h`, `src/util/TaskWatchdog.h`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Read an EPUB and keep my place (Priority: P1) 🎯 MVP

**Goal**: Open an EPUB, paginate, turn pages, persist and restore the position across sleep, reboot and layout changes.

**Independent Test**: Open a fresh EPUB, turn pages, sleep/wake, change font size; same text is shown.

### Tests for User Story 1

- [X] T025 [P] [US1] ZIP reader suite incl. lying sizes and garbage deflate in `test/zip_file/`
- [X] T026 [P] [US1] Metadata cache round trip, truncation and version rejection in `test/book_metadata_cache/`
- [X] T027 [P] [US1] CSS parser cascade, caps, cache and malformed corpus in `test/css_parser/`
- [X] T028 [P] [US1] Chapter HTML parser corpus and invariants in `test/chapter_html_slim_parser/`
- [X] T029 [P] [US1] Section build/anchor/resume/cache validation suites in `test/epub_section/`
- [X] T030 [P] [US1] Line breaking, justification, hyphen semantics and arena validation in `test/text_block_layout/`, `test/token_boundary/`, `test/hyphenation_eval/`, `test/alloc_guards/`
- [X] T031 [P] [US1] Image header probing and pixel writer/dither suites in `test/image_dims_probe/`, `test/direct_pixel_writer/`

### Implementation for User Story 1

- [X] T032 [US1] ZIP central-directory reader with streaming inflate, size validation and batch size lookup in `lib/ZipFile/ZipFile.{h,cpp}`, `lib/miniz/src/InflateStream.*`
- [X] T033 [P] [US1] Container, OPF, nav and NCX streaming parsers in `lib/Epub/Epub/parsers/ContainerParser.*`, `ContentOpfParser.*`, `TocNavParser.*`, `TocNcxParser.*`, `lib/XmlParserUtils/XmlParserUtils.h`
- [X] T034 [US1] Two-pass `book.bin` builder/loader with cumulative sizes and validation in `lib/Epub/Epub/BookMetadataCache.{h,cpp}`
- [X] T035 [US1] EPUB facade: load, CSS collection/dedup/cache, cover and thumbnail generation, progress calculation, item streaming in `lib/Epub/Epub.{h,cpp}`
- [X] T036 [P] [US1] Bounded CSS subset parser with flat rule store and partial-cache retry in `lib/Epub/Epub/css/CssParser.{h,cpp}`, `lib/Epub/Epub/css/CssStyle.h`
- [X] T037 [US1] Chapter HTML slim parser (tags, entities, ruby, tables, links, footnotes, anchors, visible offsets, images) in `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.{h,cpp}`, `lib/Epub/Epub/htmlEntities.*`, `lib/Epub/Epub/VisibleTextUtils.h`
- [X] T038 [US1] Tokenisation, line breaking (DP and greedy), justification, ruby overhang, bidi order and focus reading in `lib/Epub/Epub/ParsedText.{h,cpp}`, `lib/Epub/Epub/TokenBoundary.h`
- [X] T039 [P] [US1] Liang hyphenation with flash tries for ten languages in `lib/Epub/Epub/hyphenation/*`, `scripts/generate_hyphenation_trie.py`
- [X] T040 [US1] Flat-arena text blocks, image blocks, block styles and page model with footnotes and links in `lib/Epub/Epub/blocks/*`, `lib/Epub/Epub/Page.{h,cpp}`, `lib/Epub/Epub/PageLink.h`, `lib/Epub/Epub/FootnoteEntry.h`
- [X] T041 [US1] Section cache with header-as-key, incremental builds, suspend/resume partials, HTML cache and LUTs in `lib/Epub/Epub/Section.{h,cpp}`, `lib/Epub/Epub/ReaderRenderSpec.h`
- [X] T042 [P] [US1] Image pipeline: header probe, lazy extraction, JPEG/PNG framebuffer decoders with heap gates, Bayer dither, `.pxc` cache and direct pixel writer in `lib/Epub/Epub/converters/*`
- [X] T043 [US1] Reader base activity (validation, orientation, recents, end-of-book, page-turn detection) in `src/activities/reader/ReaderActivity.{h,cpp}`, `src/activities/reader/ReaderUtils.h`
- [X] T044 [US1] EPUB reader activity: open/restore, page turns, background pagination, prewarm, grayscale rendering, progress save, status bar, error states in `src/activities/reader/EpubReaderActivity.{h,cpp}`, `src/activities/reader/EpubReaderUtils.h`
- [X] T045 [P] [US1] Atomic progress writer in `src/activities/reader/ProgressFile.h`

**Checkpoint**: User Story 1 fully functional and testable independently

---

## Phase 4: User Story 2 - Find and open books on the SD card (Priority: P1)

**Goal**: Home, file browser, recent books, cover thumbnails, image viewer.

**Independent Test**: Browse folders, open a book, return Home, see it in Continue Reading and Recent Books.

### Tests for User Story 2

- [X] T046 [P] [US2] Recent books store persistence, eviction and pruning in `test/persistable_stores/PersistableStoresTest.cpp` (RecentBooksTest)

### Implementation for User Story 2

- [X] T047 [US2] Home screen with Continue Reading tile, menu, OPDS row, lazy thumbnails and cover-tile snapshot in `src/activities/home/HomeActivity.{h,cpp}`
- [X] T048 [US2] File browser (listing, filters, hidden files, delete, firmware-picker mode, path band) in `src/activities/home/FileBrowserActivity.{h,cpp}`
- [X] T049 [P] [US2] Recent books screen and store in `src/activities/home/RecentBooksActivity.{h,cpp}`, `src/RecentBooksStore.{h,cpp}`
- [X] T050 [P] [US2] Next-book finder and book-cache utilities in `src/util/NextBookFinder.*`, `src/util/BookCacheUtils.*`
- [X] T051 [P] [US2] Image viewer with sibling navigation and Set Cover in `src/activities/util/BmpViewerActivity.{h,cpp}`
- [X] T052 [P] [US2] Confirmation and full-screen message activities in `src/activities/util/ConfirmationActivity.*`, `src/activities/util/FullScreenMessageActivity.*`
- [X] T053 [P] [US2] Cover/thumbnail converters (JPEG and PNG to dithered BMP) in `lib/JpegToBmpConverter/*`, `lib/PngToBmpConverter/*` with suites `test/jpeg_to_bmp/`, `test/png_decode/`

**Checkpoint**: User Stories 1 and 2 form the MVP

---

## Phase 5: User Story 3 - Turn on, sleep, wake and recover safely (Priority: P1)

**Goal**: Boot routing, sleep screens, quick resume, wake verification, crash-loop guard, recovery mode.

**Independent Test**: Auto-sleep after 1 minute, wake without splash, Quick Resume returns to the page, crash boot lands on Home.

### Tests for User Story 3

- [X] T054 [P] [US3] State store round trip, sleep-image rings and settings migrations in `test/persistable_stores/PersistableStoresTest.cpp` (StateTest, SettingsTest)

### Implementation for User Story 3

- [X] T055 [US3] Boot splash and sleep screens (Dark/Light/Custom/Cover/Cover+Custom/None/Quick Resume/Transparent) with random selection history and overlay validation in `src/activities/boot_sleep/BootActivity.*`, `src/activities/boot_sleep/SleepActivity.{h,cpp}`
- [X] T056 [US3] Deep-sleep entry, quick-resume frame save/restore, wake verification, USB-power policy, silent restart and crash-loop guard in `src/main.cpp`
- [X] T057 [P] [US3] Crash screen in `src/activities/home/CrashActivity.{h,cpp}`
- [X] T058 [P] [US3] USB Serial/JTAG hand-off and eFuse check bypass in `src/platform/UsbSerialJtagHandoff.*`, `src/platform/skip_efuse_blk_check.c`

**Checkpoint**: Device is safe to ship: sleeps, wakes and recovers

---

## Phase 6: User Story 4 - Navigate inside a book (Priority: P2)

**Goal**: Reader menu, chapters, footnotes/links, bookmarks, go-to-percent, end-of-book suggestions.

**Independent Test**: Use every reader-menu row on a book with TOC and footnotes.

### Tests for User Story 4

- [X] T059 [P] [US4] Link hit-test slop and minimum width in `test/page_link/`

### Implementation for User Story 4

- [X] T060 [US4] Reader menu list with conditional rows and result contract in `src/activities/reader/EpubReaderMenuActivity.{h,cpp}`
- [X] T061 [P] [US4] Windowed chapter selection in `src/activities/reader/EpubReaderChapterSelectionActivity.{h,cpp}`
- [X] T062 [P] [US4] Footnote list and return stack handling in `src/activities/reader/EpubReaderFootnotesActivity.{h,cpp}` and `EpubReaderActivity.cpp`
- [X] T063 [P] [US4] Bookmarks list, toggle, JSON store and path/summary helpers in `src/activities/reader/EpubReaderBookmarksActivity.{h,cpp}`, `src/util/BookmarkFile.*`, `src/util/BookmarkUtil.*`, `src/BookmarkEntry.h`
- [X] T064 [P] [US4] Go-to-percent slider dialog in `src/activities/reader/EpubReaderPercentSelectionActivity.{h,cpp}`
- [X] T065 [P] [US4] End-of-book options and touch link following in `src/activities/reader/EndOfBookOptions.{h,cpp}`, `EpubReaderActivity.cpp`
- [X] T066 [US4] Toolbar overlay menu with Contents/Text/More sheets in `src/activities/reader/ReaderToolbarUi.{h,cpp}`

**Checkpoint**: In-book navigation complete

---

## Phase 7: User Story 5 - Tune typography, layout and display (Priority: P2)

**Goal**: Text settings with preview, status bar composition, orientation, night mode, refresh cadence, themes.

**Independent Test**: Change every Text Settings row and see the preview and the book follow.

### Implementation for User Story 5

- [X] T067 [US5] Settings screen with four tabs, pickers, board pruning and immediate saves in `src/activities/settings/SettingsActivity.{h,cpp}`
- [X] T068 [US5] Text Settings tabs with engine-rendered preview and point-size snapping in `src/activities/settings/TextSettingsActivity.{h,cpp}`, `src/activities/settings/TextSettingsPreview.{h,cpp}`, `src/ReaderFontSizes.*`
- [X] T069 [P] [US5] Customise Status Bar with live preview, clock offset picker and NTP sync in `src/activities/settings/StatusBarSettingsActivity.{h,cpp}`, `src/activities/settings/ClockOffsetActivity.*`, `src/activities/settings/ClockSyncActivity.*`
- [X] T070 [P] [US5] Clear Reading Cache flow in `src/activities/settings/ClearCacheActivity.{h,cpp}`
- [X] T071 [P] [US5] Anti-aliased strip/chunked grayscale rendering and refresh cadence in `src/activities/reader/EpubReaderActivity.cpp`, `src/activities/reader/ReaderUtils.h`

**Checkpoint**: Typography and display configurable end to end

---

## Phase 8: User Story 6 - Read TXT, XTC and FB2 books (Priority: P2)

**Goal**: Format-specific readers with caches, progress and covers.

**Independent Test**: Open one file of each format, turn pages, sleep/wake, resume.

### Tests for User Story 6

- [X] T072 [P] [US6] XTC container parser suite with malformed fixtures in `test/xtc_parser/`, `scripts/generate_test_xtc.py`
- [X] T073 [P] [US6] FB2 metadata, section parser, section cache, cover extractor, book and filename suites in `test/fb2_*/`, `scripts/generate_fb2_test_fixtures.py` *(fork-only)*
- [X] T074 [P] [US6] HTML-to-plain-text suite in `test/html_to_plain_text/`

### Implementation for User Story 6

- [X] T075 [P] [US6] Streaming TXT reader with page-index cache and sidecar cover in `src/activities/reader/TxtReaderActivity.{h,cpp}`, `lib/Txt/Txt.{h,cpp}`
- [X] T076 [P] [US6] XTC parser, reader, chapter selection, cover/thumbnail generation in `lib/Xtc/**`, `src/activities/reader/XtcReaderActivity.*`, `src/activities/reader/XtcReaderChapterSelectionActivity.*`
- [X] T077 [P] [US6] FB2 library (metadata, encodings, sections, cover) and reader in `lib/Fb2/**`, `src/activities/reader/Fb2ReaderActivity.*`, `src/activities/reader/Fb2ReaderChapterSelectionActivity.*` *(fork-only)*
- [X] T078 [P] [US6] HTML-to-plain-text utility in `src/util/HtmlToPlainText.*`

**Checkpoint**: All four book formats readable

---

## Phase 9: User Story 7 - Read in any script with SD-card fonts (Priority: P2)

**Goal**: `.cpfont` loading, registry/manager, CJK UI fallback, on-device font download, web font API.

**Independent Test**: Download a CJK family, select it, read a CJK book, see CJK file names.

### Tests for User Story 7

- [X] T079 [P] [US7] SD font loading, prewarm, overflow ring, advance table and registry suites in `test/sdcard_font/`, `scripts/generate_test_cpfonts.py`
- [X] T080 [P] [US7] Font decompressor golden, cache manager, differential rounding, ligature guard, combining marks, bidi and NFC suites in `test/font_decompressor/`, `test/font_cache_manager/`, `test/differential_rounding/`, `test/ligature_guard/`, `test/combining_marks/`, `test/minibidi_arabic/`, `test/utf8_compose/`

### Implementation for User Story 7

- [X] T081 [US7] `.cpfont` v4 loader with mini arenas, overflow ring, advance tables and kern matrices in `lib/EpdFont/SdCardFont.{h,cpp}`
- [X] T082 [P] [US7] Font registry (roots, discovery, nearest size) and manager (font ids, fallbacks) in `lib/EpdFont/SdCardFontRegistry.*`, `lib/EpdFont/SdCardFontManager.*`
- [X] T083 [US7] Font system glue: boot load, CJK UI fallbacks, dirty re-scan, installer validation in `src/SdCardFontSystem.*`, `src/FontInstaller.*`
- [X] T084 [P] [US7] Font browser with manifest download, groups, CRC-verified installs and deletion in `src/activities/settings/FontDownloadActivity.{h,cpp}`
- [X] T085 [P] [US7] Font conversion and catalog tooling in `lib/EpdFont/scripts/*`, `scripts/generate-font-manifest.py`

**Checkpoint**: Any script readable with SD fonts

---

## Phase 10: User Story 8 - Transfer files wirelessly or over USB (Priority: P2)

**Goal**: Wi-Fi selection, web server and pages, HTTP/WebSocket/WebDAV, Calibre, hotspot, USB Drive, HTTP client.

**Independent Test**: Upload from the browser, mount WebDAV, send from Calibre, use the hotspot.

### Tests for User Story 8

- [X] T086 [P] [US8] WebDAV path confinement and handler behaviour in `test/web_dav_paths/`
- [X] T087 [P] [US8] Wi-Fi credential obfuscation, integrity and limits in `test/persistable_stores/PersistableStoresTest.cpp` (WifiTest), `test/credential_integrity/`

### Implementation for User Story 8

- [X] T088 [US8] Wi-Fi selection with auto-connect, scan list, hidden SSID, password/save/forget prompts in `src/activities/network/WifiSelectionActivity.{h,cpp}`, `src/WifiCredentialStore.{h,cpp}`
- [X] T089 [US8] Web server: pages, status/files/settings/fonts/OPDS/Wi-Fi APIs, WebSocket upload, UDP discovery, CORS/captive redirect in `src/network/CrossPointWebServer.{h,cpp}`
- [X] T090 [P] [US8] WebDAV Class 1 handler in `src/network/WebDAVHandler.{h,cpp}`
- [X] T091 [P] [US8] Browser UI pages (file manager with EPUB optimiser, settings, fonts, home) in `src/network/html/*.html`, `src/network/html/js/jszip.min.js`
- [X] T092 [P] [US8] Mode selection, server activity (STA/AP/QR/RSSI), Calibre screen and USB Drive activity in `src/activities/network/NetworkModeSelectionActivity.*`, `CrossPointWebServerActivity.*`, `CalibreConnectActivity.*`, `UsbDriveActivity.*`
- [X] T093 [P] [US8] Shared HTTP(S) client and URL utilities in `src/network/HttpDownloader.{h,cpp}`, `src/util/UrlUtils.*`

**Checkpoint**: Books reach the card without removing it

---

## Phase 11: User Story 9 - Browse and download from OPDS catalogs (Priority: P2)

**Goal**: Server store and screens, feed parsing, browsing, search, pagination, download naming.

**Independent Test**: Add a server, browse, search, download, open.

### Tests for User Story 9

- [X] T094 [P] [US9] OPDS parser suite and filename suite in `test/opds_parser/`, `test/opds_filename/`; server store persistence in `test/persistable_stores/` (OpdsTest)

### Implementation for User Story 9

- [X] T095 [P] [US9] Streaming OPDS parser and stream wrapper in `lib/OpdsParser/*`
- [X] T096 [P] [US9] Server store and settings screens (list/picker, editor, folder, filename format) in `src/OpdsServerStore.*`, `src/activities/settings/OpdsServerListActivity.*`, `src/activities/settings/OpdsSettingsActivity.*`
- [X] T097 [US9] Catalog browser with pagination, search and download in `src/activities/browser/OpdsBookBrowserActivity.{h,cpp}`, `src/util/OpdsFilename.*`

**Checkpoint**: OPDS acquisition works end to end

---

## Phase 12: User Story 10 - Configure controls, input and the frontlight (Priority: P2)

**Goal**: Remap wizard, power-button modes, tilt, control center, keyboard.

**Independent Test**: Remap buttons, change power click mode, use the control center.

### Implementation for User Story 10

- [X] T098 [P] [US10] Front-button remap wizard in `src/activities/settings/ButtonRemapActivity.{h,cpp}`
- [X] T099 [P] [US10] Frontlight control center with sliders and quick tiles in `src/activities/util/FrontlightPanelActivity.{h,cpp}`
- [X] T100 [P] [US10] Keyboard entry activity, layout set and layouts screen in `src/activities/util/KeyboardEntryActivity.{h,cpp}`, `src/activities/util/KeyboardLayoutSet.*`, `src/activities/settings/KeyboardLayoutsActivity.*`
- [X] T101 [P] [US10] Interval selection dialog (sleep timeout) in `src/activities/util/IntervalSelectionActivity.{h,cpp}`
- [X] T102 [US10] Power-button modes, X4 Pro double-click, tilt polling and control-center gestures wired in `src/main.cpp`, `src/activities/ActivityManager.cpp`, `src/MappedInputManager.cpp`

**Checkpoint**: Input fully configurable

---

## Phase 13: User Story 11 - Use the device in my language (Priority: P2)

**Goal**: 34 UI languages, language screen, keyboard layouts.

**Independent Test**: Switch language; verify persistence and English fallback.

### Implementation for User Story 11

- [X] T103 [US11] Language selection screen (English first, BCP 47 order) in `src/activities/settings/LanguageSelectActivity.{h,cpp}`
- [X] T104 [P] [US11] Translator documentation and generator validation rules in `docs/i18n.md`, `docs/translators.md`, `scripts/gen_i18n.py`

**Checkpoint**: Localised UI complete

---

## Phase 14: User Story 12 - Look up words in an offline dictionary (Priority: P3)

**Goal**: StarDict reader with dictzip, sidecars, word selection and definition screens.

**Independent Test**: Select a dictionary, look up words, see HTML and plain definitions.

### Tests for User Story 12

- [X] T105 [P] [US12] Dictionary and dictzip suites with generated fixtures in `test/dictionary/`, `test/dict_zip/`, `test/dict_common/`, `scripts/generate_test_dict.py`

### Implementation for User Story 12

- [X] T106 [P] [US12] StarDict reader, sidecars, lookup and stemming in `src/util/Dictionary.{h,cpp}`; dictzip random access in `src/util/DictZip.{h,cpp}`
- [X] T107 [P] [US12] Dictionary discovery and HTML definition layout in `src/util/DictionaryRegistry.*`, `src/util/DictHtmlPages.*`
- [X] T108 [US12] Word selection and definition activities in `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}`, `src/activities/reader/DictionaryDefinitionActivity.{h,cpp}`

**Checkpoint**: Dictionary usable from the reader

---

## Phase 15: User Story 13 - Sync reading progress with KOReader (Priority: P3)

**Goal**: KOSync client, credential store, document ids, xpointer generation/resolution, sync and auth screens.

**Independent Test**: Authenticate, sync in Smart and Ask modes.

### Tests for User Story 13

- [X] T109 [P] [US13] Progress mapper and xpointer resolver suites with pinned limitations in `test/progress_mapper/`, `test/chapter_xpath_resolver/`, `test/kosync_common/`; credential store in `test/persistable_stores/` (KoReaderTest)

### Implementation for User Story 13

- [X] T110 [P] [US13] KOSync HTTP client with heap gate, position extension and metadata in `lib/KOReaderSync/KOReaderSyncClient.{h,cpp}`
- [X] T111 [P] [US13] Credential store with default-server migration and document-id derivation in `lib/KOReaderSync/KOReaderCredentialStore.*`, `lib/KOReaderSync/KOReaderDocumentId.*`
- [X] T112 [US13] Content-anchored position mapping (xpointer generation and tolerant resolution) in `lib/KOReaderSync/ProgressMapper.{h,cpp}`, `lib/KOReaderSync/ChapterXPathResolver.{h,cpp}`
- [X] T113 [US13] Sync activity (Smart/Ask flows), settings and auth/sign-up screens in `src/activities/reader/KOReaderSyncActivity.{h,cpp}`, `src/activities/settings/KOReaderSettingsActivity.*`, `src/activities/settings/KOReaderAuthActivity.*`

**Checkpoint**: Progress syncs with KOReader-compatible servers

---

## Phase 16: User Story 14 - Update the firmware without a computer (Priority: P3)

**Goal**: OTA from GitHub releases, SD firmware update, recovery mode.

**Independent Test**: OTA from an older release; SD update with valid and invalid images; recovery boot.

### Tests for User Story 14

- [X] T114 [P] [US14] Asset selection, semver rules, board tag scanner, streaming install and release/streaming JSON parser suites in `test/ota_asset_selection/`, `test/release_json_parser/`, `test/streaming_json_parser/`

### Implementation for User Story 14

- [X] T115 [P] [US14] Bounded streaming JSON and release parsers in `lib/JsonParser/*`
- [X] T116 [P] [US14] OTA updater, board tag and boot-partition switch in `src/network/OtaUpdater.*`, `src/network/FirmwareBoardTag.*`, `src/network/OtaBootSwitch.*`
- [X] T117 [P] [US14] SD image validation and raw flasher in `src/network/FirmwareFlasher.{h,cpp}`
- [X] T118 [US14] OTA and SD update activities (incl. recovery mode) in `src/activities/settings/OtaUpdateActivity.*`, `src/activities/settings/SdFirmwareUpdateActivity.*`

**Checkpoint**: Field updates possible on every board

---

## Phase 17: User Story 15 - Reader conveniences (Priority: P3)

**Goal**: Auto page turn, screenshots, QR export.

**Independent Test**: Auto Turn at 3 ppm; Power+Down screenshot; QR screen.

### Implementation for User Story 15

- [X] T119 [P] [US15] Auto page turn timer and status label in `src/activities/reader/EpubReaderActivity.cpp`
- [X] T120 [P] [US15] Screenshot writer with rotation and per-book naming in `src/util/ScreenshotUtil.*`, `src/util/ScreenshotInfo.h`
- [X] T121 [P] [US15] QR encoding and display in `src/util/QrUtils.*`, `src/activities/reader/QrDisplayActivity.*`

**Checkpoint**: Conveniences available in the reader

---

## Phase 18: User Story 16 - Diagnose crashes and problems (Priority: P3)

**Goal**: Crash reports, serial tooling, host test program and CI.

**Independent Test**: Force a panic; verify the report, the crash screen, the serial screenshot command; run the test program plain and under ASan.

### Implementation for User Story 16

- [X] T122 [P] [US16] Serial debugging monitor with memory graph and screenshot capture in `scripts/debugging_monitor.py`
- [X] T123 [P] [US16] Host test runner, PlatformIO target, sanitizer option and corpus support in `bin/run-tests`, `scripts/register_unit_tests_target.py`, `test/CMakeLists.txt`, `test/support/*`, `test/corpus/*`, `scripts/generate_test_corpus.py` *(fork-only)*
- [X] T124 [P] [US16] Parser-utility, persistable-store and XML-utility suites in `test/xml_parser_utils/`, `test/persistable_stores/`

**Checkpoint**: All user stories independently functional

---

## Phase 19: Polish & Cross-Cutting Concerns

- [X] T125 [P] User and contributor documentation in `README.md`, `USER_GUIDE.md`, `docs/*.md`, `docs/contributing/*.md`
- [X] T126 [P] Project scope, roadmap and governance in `SCOPE.md`, `ROADMAP.md`, `GOVERNANCE.md`
- [X] T127 [P] Fork constitution, spec-kit scaffolding and autocommit extension in `.specify/**`
- [X] T128 Retrospective specification, plan, research, data model, contracts and quickstart in `specs/001-crosspoint-reader-baseline/`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)** and **Foundational (Phase 2)** precede every story.
- **US1 (Phase 3)** and **US2 (Phase 4)** together form the MVP; **US3 (Phase 5)** is required before shipping.
- **US4, US5, US15** extend the EPUB reader (Phase 3); **US6** reuses the reader base and layout pipeline; **US7** extends the renderer and fonts; **US8** is the prerequisite for **US9, US13, US14** and the font download in **US7**; **US12** depends on the EPUB page model; **US10, US11, US16** depend only on the foundation.

### Parallel Opportunities

- Within the foundation, HAL wrappers, memory/serialization helpers, fonts, bidi and i18n are independent.
- Format readers (US6), font tooling (US7), and the network client stack (US8) can proceed in parallel once the foundation exists.

---

## Implementation Strategy

This list is retrospective: every task above is delivered. The value of the list is traceability (story → files) for future features and for `/speckit-converge`, which appends the remaining work below as numbered convergence phases.

---

## Notes

- Fork-only work is marked *(fork-only)*; it stays out of upstream PR branches (Constitution VII).
- Convergence phases are append-only; existing task IDs are never renumbered.
