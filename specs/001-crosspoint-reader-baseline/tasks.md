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

---

## Phase 20: Convergence

**Purpose**: Remaining work found by assessing the code against spec.md, plan.md and the constitution on 2026-09-15. Ordered CRITICAL → LOW. Constitution violations first.

### CRITICAL — Constitution V (host tests for host-reachable logic) and VI (malformed-input corpus)

- [X] T129 CRITICAL: Add host suites for the EPUB container/OPF/nav/NCX parsers and `Epub.cpp` orchestration (cover selection, CSS retry state machine, `resolveHrefToSpineIndex`, `calculateProgress`) with a malformed-input corpus (truncation, lying sizes, hostile nesting, encoding abuse) in `test/content_opf_parser/` (mirror upstream's suite name) and `test/epub_orchestration/`, compiling `lib/Epub/Epub.cpp` and `lib/Epub/Epub/parsers/{Container,ContentOpf,TocNav,TocNcx}Parser.cpp` against the in-tree expat per Constitution V, VI and FR-044..FR-046 (missing)
- [X] T130 CRITICAL: Extract the TXT pagination, `index.bin` and progress logic from `src/activities/reader/TxtReaderActivity.cpp` into a host-compilable unit, add a suite with an `index.bin`/`progress.bin` corpus, and cover `lib/Txt/Txt.cpp` (title, cover discovery, BMP copy/JPEG convert/PNG reject) per Constitution V, VI and FR-096..FR-098 (missing)
- [X] T131 CRITICAL: Add a reader-helpers suite covering `src/util/BookmarkUtil.cpp`, `src/util/BookmarkFile.cpp`, `src/activities/reader/ProgressFile.h` (temp+remove+rename, short writes), `src/activities/reader/EpubReaderUtils.h` (progress byte layout, range checks, `linkAtPoint`), `src/activities/reader/ReaderUtils.h` (page-turn/touch/menu detection, refresh countdown, back navigation), percent wrap in `EpubReaderPercentSelectionActivity.cpp`, `src/util/QrUtils.cpp`, `EndOfBookOptions.cpp` and `EpubReaderMenuActivity.cpp` row ordering per Constitution V and FR-076, FR-085..FR-093 (missing)
- [X] T132 CRITICAL: Add a library-helpers suite covering `src/util/NextBookFinder.cpp`, `src/util/BookCacheUtils.cpp`, `src/components/UITheme.cpp` (`getCoverThumbPath`, `getFileIcon`), `src/util/ButtonNavigator.cpp`, `lib/FsHelpers/FsHelpers.cpp` (`naturalLess`, `sortFileList`, extension predicates, `extractFolderPath`, `sanitizePathComponentForFat32`) and `HomeActivity` menu index mapping per Constitution V and FR-029, FR-032, FR-036, FR-039 (missing)
- [X] T133 CRITICAL: Add rendering/font suites: `lib/GfxRenderer/GfxRenderer.cpp` behind a `HalDisplay` stub (rotation, fill masks, text width/advance agreement, SUP/SUB, truncation/wrapping, strip clipping, BW snapshot chunks), `lib/GfxRenderer/Bitmap.cpp` header validation, `createBmpHeader` layout, `lib/EpdFont/EpdFontFamily.cpp` fallback order, `lib/EpdFont/SdCardFontManager.cpp`, `src/FontInstaller.cpp` (path-traversal rejection, root selection), `src/SdCardFontSystem.cpp` (reload/snap/clear matrix, CJK probing), `src/ReaderFontSizes.cpp`, `lib/MiniBidi/BidiUtils.cpp` word order/paragraph level/overflow, `lib/Utf8/Utf8.cpp` malformed decode and truncation, `SdCardFont` arena-retry/hysteresis, `FontDownloadActivity` manifest parsing (extracted) per Constitution V and FR-107..FR-124 (missing)
- [X] T134 CRITICAL: Add settings/input suites covering `src/activities/util/KeyboardLayoutSet.cpp`, `src/MappedInputManager.cpp` (mapping, axis swap, long-press suppression, labels) behind stubs, `ClockOffsetActivity` encode/decode/clamp, `CrossPointSettings` derived getters (`getSleepTimeoutMs`, `getRefreshFrequency`, `readerRenderSpec`, `clearSdFontFamily`), `lib/I18n/I18n.cpp` bit-15 lookup and bounds, `lib/hal/HalTiltSensor.cpp` state machine behind an IMU stub, keyboard UTF-8 helpers (extracted) and `syncQuickResumeTimeoutForSleepScreen` per Constitution V and FR-125..FR-141 (missing)
- [X] T135 CRITICAL: Add network suites: `src/util/UrlUtils.cpp`; extract `normalizeWebPath`, `isProtectedItemName`, the WebSocket `START` parser and `barsForRssi` from the Wi-Fi-bound translation units into host-compilable helpers and cover them; cover scan de-duplication/sorting and WebDAV `getDepth`/`getOverwrite`/`getMimeType` per Constitution V, III and FR-146, FR-150..FR-152, FR-156, FR-159 (missing)
- [X] T136 CRITICAL: Add OPDS/OTA/persistence suites covering `src/network/FirmwareFlasher.cpp` (`validateImageFile` every rejection path, `flashFromSdPath` erase/write cadence) and `src/network/OtaBootSwitch.cpp` behind `esp_partition` stubs, `lib/Serialization/Serialization.h` and `BufferedFile.h` directly (bounds, passthrough on OOM, in-window seek), `OpdsServerListActivity` `normalizeFolder`, `OpdsBookBrowserActivity` `performSearch` encoding and pagination rows (extracted), the bounded `extractPassword` overload and `InflateReader::initWithRing` per Constitution V and FR-162..FR-167 (missing)
- [X] T137 CRITICAL: Add KOReader suites covering `lib/KOReaderSync/KOReaderDocumentId.cpp` (offset schedule, EOF skipping, basename), `lib/KOReaderSync/KOReaderSyncClient.cpp` status→error mapping and request/response JSON (position gating, 120-byte xpath cap) behind a `SecureHttpClient` stub, `KOReaderCredentialStore::getBaseUrl`/`getMd5Password`, the Smart-sync decision table extracted from `KOReaderSyncActivity.cpp`, rich-position construction, settings URL handling, `generateXPath` byte fallback and the anchor-id attribute scanner per Constitution V and FR-169..FR-176 (missing)
- [X] T138 CRITICAL: Compile `src/util/DictionaryRegistry.cpp` in `test/dictionary/` instead of link-stubbing it (ambiguous stems, `._` files, dot folders, name validation, sort), and add coverage for `src/util/DictHtmlPages.cpp` normalisation and heap/page/element gates, `isSelectableToken`/`closestInRow` and `wrapText` (extracted from the dictionary activities), sidecar yield cadence, `.ifo` edge cases and `stemVariants` per Constitution V and FR-177..FR-182 (missing)
- [X] T139 CRITICAL: Add platform suites covering `lib/Memory/BuildScratch.cpp` lend/claim/release/reclaim, `lib/Logging/Logging.cpp` ring wrap/ordering/magic sanitising, `lib/ZipFile` `fillUncompressedSizes`/sequential cursor/early stop, `lib/Memory/Memory.h` helpers, `lib/hal/HalSystem.cpp` `getPanicInfo`/reset classification behind stubs, `src/util/ScreenshotUtil.cpp` `buildFilename`, `lib/hal/HalClock.cpp` `formatTime`, and the sleep-image placement/header validators extracted from `src/activities/boot_sleep/SleepActivity.cpp` per Constitution V and FR-017, FR-028, FR-185..FR-188 (missing)
- [X] T140 CRITICAL: Remove the direct FreeRTOS includes from `lib/Xtc/Xtc.cpp` and add coverage for its cover/thumbnail conversion (XTH plane → 2-bit BMP, 1-bit padding, scale/crop, no-upscale copy), `XtcReaderActivity` status-bar math and progress clamping, `XtcReaderChapterSelectionActivity::findChapterIndexForPage`, `Fb2ReaderActivity` percent mapping/page rescaling/progress decoding, `lib/Fb2/Fb2/Fb2XmlEncoding.cpp` tables and the `hasXtc/Txt/MarkdownExtension` predicates per Constitution V, III and FR-099..FR-105 (missing)

### CRITICAL — Constitution VI (validate untrusted lengths before allocation)

- [X] T141 CRITICAL: In `src/activities/reader/TxtReaderActivity.cpp` `loadPageIndexCache`, check every `readPod` return value and validate `numPages` against the file size and a sane cap before `pageOffsets.reserve()`; reject and rebuild on mismatch per Constitution VI and FR-096 (partial)
- [X] T142 CRITICAL: In `lib/EpdFont/SdCardFont.cpp`, validate each glyph record (`width*height*bpp/8 <= dataLength`, `dataOffset + dataLength` within the bitmap section) and every TOC section offset against the physical file size before use, and clamp bitmap reads in `lib/GfxRenderer/GfxRenderer.cpp` `renderCharImpl` per Constitution VI and FR-116 (partial)
- [X] T143 CRITICAL: Make `lib/Epub/Epub/Page.cpp` `PageImage::deserialize` reject a null or dimension-invalid `ImageBlock`, and make `lib/Epub/Epub/BookMetadataCache.cpp` `getSpineEntry`/`getTocEntry` honour `readPod`/`readString` failures and validate every LUT slot at load per Constitution VI and FR-047, FR-052 (partial)
- [X] T144 CRITICAL: Bound untrusted string fields before allocation in `src/OpdsServerStore.cpp` (name/url/username and the bounded `extractPassword` overload), `src/RecentBooksStore.cpp` and `lib/Serialization/PersistableStore.cpp` (cap the whole-file read and per-field lengths), `lib/KOReaderSync/KOReaderSyncClient.cpp` (response body and string fields), the web `POST /api/opds` and `POST /api/settings` handlers in `src/network/CrossPointWebServer.cpp`, and the manifest document in `src/activities/settings/FontDownloadActivity.cpp` per Constitution VI and FR-125, FR-133, FR-153, FR-161, FR-170 (partial)
- [X] T145 CRITICAL: Normalise and validate client-supplied paths and filenames (`normalisePath`, reject `..`, `/`, `\\`, dot-prefixed and protected names) for `POST /upload`, the WebSocket `START` path/filename, `POST /mkdir`, `GET /api/files`, `GET /download` and `POST /delete` in `src/network/CrossPointWebServer.cpp`, matching the checks `/rename`, `/move` and the font upload already perform per Constitution VI and FR-150..FR-152 (partial)

### CRITICAL — Constitution II (no bare `new` for fallible allocations)

- [X] T146 CRITICAL: Replace `std::make_unique`/bare `new` on fallible paths with `makeUniqueNoThrow` (or `new (std::nothrow)` with null checks) in `src/activities/ActivityManager.cpp` (`goTo*` helpers), `src/main.cpp` (recovery route), `src/activities/reader/EpubReaderActivity.cpp` (`Section`, toolbar, child activities), `src/util/QrUtils.cpp`, `lib/Epub/Epub.cpp`, `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`, `lib/Epub/Epub/Page.cpp`, `lib/Epub/Epub/ParsedText.cpp`, `src/activities/settings/SettingsActivity.cpp`, `StatusBarSettingsActivity.cpp`, `ClockSyncActivity.cpp`, `src/network/CrossPointWebServer.cpp` and `src/activities/network/*` (`WebServer`, `WebSocketsServer`, `WebDAVHandler`, `CrossPointWebServer`, `DNSServer`), `lib/PngToBmpConverter/PngToBmpConverter.cpp` and `lib/GfxRenderer/BitmapHelpers.h` (ditherers), `lib/GfxRenderer/Bitmap.cpp` and `lib/Xtc/Xtc.cpp` (`XtcParser`) per Constitution II and FR-190 (contradicts)

### CRITICAL — Platform Constraints (expat parity)

- [X] T147 CRITICAL: Switch `test/opds_parser/CMakeLists.txt` and `test/chapter_html_slim_parser/CMakeLists.txt` from the host system expat to the in-tree expat compiled with `XML_GE=0` and `XML_CONTEXT_BYTES=1024`, update the html-corpus pins accordingly, and fix `test/corpus/README.md` per Constitution "Platform Constraints & Standards" (XML) and FR-192 (contradicts)

### HIGH

- [X] T148 Add a `unit-tests-asan` job to `.github/workflows/ci.yml` that configures with `-DCROSSPOINT_SANITIZE=ON` (mirroring `bin/run-tests --asan`) and include it in the `test-status` aggregate per Constitution V and quality gate 3 (missing)
- [X] T149 Either make the reader honour `SETTINGS.pwrBtnFootnoteBack` when a short Power click returns from a footnote in `src/activities/reader/EpubReaderActivity.cpp`, or remove the "Quick-return from footnotes" row from `src/SettingsList.h` and its key from `settings.json`; update USER_GUIDE §3.6.3 to match per FR-198 and Constitution IV (contradicts)
- [X] T150 Add the `x4c` environment to the build matrix in `.github/workflows/ci.yml` and `x4c-gh_release` to `.github/workflows/release.yml` (asset `firmware-x4c.bin`, plus an `x4c-gh_release_rc` env in `platformio.ini` and `release_candidate.yml`) so X4 Classic devices can find OTA updates per FR-165, FR-168 (contradicts)
- [X] T151 Guard and debounce SD writes: skip unchanged progress in `src/activities/reader/TxtReaderActivity.cpp`, `XtcReaderActivity.cpp` and `Fb2ReaderActivity.cpp`; add equality guards to `src/OpdsServerStore.cpp` `updateServer` and the editor in `OpdsSettingsActivity.cpp`; collapse the three `state.json` writes per sleep cycle in `src/main.cpp`/`SleepActivity.cpp` into one; skip `SETTINGS.saveToFile()` when zero settings were applied in `POST /api/settings` (`src/network/CrossPointWebServer.cpp`) and when a reader toggle does not change the value per Constitution II and FR-076, FR-101, FR-105, FR-133, FR-162 (partial)
- [X] T152 Remove per-render `std::string`/Arduino `String` construction from hot paths: status-bar title in `src/activities/reader/EpubReaderActivity.cpp`, `.pxc` path per pass in `lib/Epub/Epub/blocks/ImageBlock.cpp`, ruby draw vector in `lib/Epub/Epub/blocks/TextBlock.cpp`, per-element class/style/dir strings in `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`, per-call visual buffers in `lib/GfxRenderer/GfxRenderer.cpp`, per-word tokens in `ensureSdCardFontReady`, per-repaint temporaries in `src/activities/util/KeyboardEntryActivity.cpp` and `src/activities/home/HomeActivity.cpp`/theme menu drawers, the serial command `String` in `src/main.cpp`, `Arduino String` in `src/activities/settings/ClearCacheActivity.cpp` and the WebSocket `PROGRESS` message in `src/network/CrossPointWebServer.cpp` per Constitution II (partial)
- [X] T153 Review and relocate stack buffers over 256 bytes to static or member storage where the task stack cannot guarantee headroom: `lib/Epub/Epub/css/CssParser.cpp` (`loadFromStream` ~2.6 KB), `lib/Epub/Epub/hyphenation/LiangHyphenation.cpp` (~1.4 KB), `lib/PngToBmpConverter/PngToBmpConverter.cpp` (`PngDecodeContext` ~2.9 KB), `src/network/OtaUpdater.cpp` (`ReleaseJsonParser` ~1.7 KB), `src/util/Dictionary.cpp` (`readIfoFacts` 2 KB), `lib/KOReaderSync/ProgressMapper.cpp` (two `ParagraphStreamer`s), `lib/EpdFont/FontDecompressor.cpp` (`prewarmCache` 2.8 KB), `lib/EpdFont/SdCardFont.cpp` (`buildMiniKernMatrix` 1.5 KB), `lib/MiniBidi/minibidi.c`, `src/network/CrossPointWebServer.cpp`/`WebDAVHandler.cpp` (4 KB copy buffers, 500-byte names), `lib/Txt/Txt.cpp`, `lib/Xtc/Xtc.cpp`, `src/activities/util/BmpViewerActivity.cpp`, `lib/KOReaderSync/KOReaderDocumentId.cpp` per Constitution II and plan.md Complexity Tracking (partial)
- [X] T154 Mask the KOReader password in `GET /api/settings` (return a `hasPassword`-style value and preserve on omitted input, as `/api/opds` and `/api/wifi` do) in `src/SettingsList.h` and `src/network/CrossPointWebServer.cpp` per FR-194 (partial)
- [X] T155 Recognise both release-asset naming schemes in `src/network/OtaUpdater.cpp` (`firmware[-<board>].bin` and upstream's `crosspoint-<tag>-<device>.bin`, mirroring upstream #3493) and flip the pinned `test/ota_asset_selection` limitation test into a regression guard per FR-165 and Constitution VII (partial)

### MEDIUM

- [X] T156 Replace `std::function` on render and library paths with function-pointer + context callbacks: theme drawing API and `std::bind` in `src/components/themes/BaseTheme.h`/`src/activities/home/HomeActivity.cpp`, `ReaderToolbarUi::Model` row callbacks, `Section`/`XtcParser`/`Fb2Section` callbacks, `scanFiles` and `HttpDownloader::Sink`, and stop copying the whole `SettingInfo` vector per `getSettingsList()` call (`src/SettingsList.h`, twice per web request) per Constitution II and plan.md Complexity Tracking (partial)
- [X] T157 Introduce a small platform seam (heap query, millis/delay, `Print` sink alias, string type) so pure-logic libraries stop including Arduino/FreeRTOS headers directly: `lib/Epub/Epub/css/CssParser.cpp`, `lib/Epub/Epub.cpp`, `lib/Epub/Epub/blocks/ImageBlock.cpp`, `lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp`, `lib/EpdFont/FontDecompressor.cpp`, `lib/EpdFont/SdCardFont.cpp`, `lib/GfxRenderer/GfxRenderer.cpp`, `src/util/Dictionary.cpp`, `DictZip.cpp`, `DictHtmlPages.cpp`, `lib/OpdsParser/OpdsParser.h`, `lib/Serialization/PersistableStore.h`, `ObfuscationUtils.h`, `lib/FsHelpers/FsHelpers.h`, `lib/JpegToBmpConverter/*`, `lib/PngToBmpConverter/*`, `lib/KOReaderSync/*`, `src/SettingsList.h`, `src/util/ButtonNavigator.h`, mirroring upstream's shape where it exists per Constitution III (partial)
- [X] T158 Validate `zipDetails.totalEntries` against the central-directory size before `reserve()` in `lib/ZipFile/ZipFile.cpp` `loadAllFileStatSlims`, and bound `BufferedFileReader` `readString` by the remaining file size in `lib/Serialization/BufferedFile.h` per Constitution VI and FR-048, FR-188 (partial)
- [X] T159 Make `HalFile` methods on a default-constructed handle return an error instead of `assert(impl != nullptr)` in `lib/hal/HalStorage.cpp` per Constitution "Error Handling" (no abort) and FR-183 (partial)
- [X] T160 Update `USER_GUIDE.md` §1, §2, §3.1–§3.5 and §7 to the shipped behaviour (screenshot paths, frontlight boards, conditional resume, Home OPDS row/Continue tile/Back=resume, browser path band/icons/hidden files/.png, no on-device rename/move/multi-select, recents cap and removal, USB Drive mode, bar signal indicator, browser-side optimiser, host-initiated serial screenshot, recovery boot) per FR-005, FR-021, FR-029..FR-037, FR-145, FR-159, FR-186, FR-187 (contradicts)
- [X] T161 Rewrite `USER_GUIDE.md` §3.6 (Display/Reader/Controls/System) to the shipped settings: Customise Status Bar sub-screen, Refresh "Never", Night Mode and Restore Light rows, Quick Resume in the §3.7 table, point-size fonts and the Text Settings tabs, Extra Wide spacing, Book's Style alignment, three-way Images, Long-press button behavior options and defaults, Long-press Menu default Disabled and Reader Menu option, Short Power "Confirm", Quick-return status per T149, omitted Controls/System rows, Time to Sleep 1..30+Never, 34 languages, Manage Fonts under Reader, SD Card Firmware Update and Keyboard Layouts per FR-011, FR-022, FR-060..FR-062, FR-070, FR-077, FR-078, FR-084, FR-091, FR-119, FR-128..FR-131, FR-142, FR-167 (contradicts)
- [X] T162 Update `USER_GUIDE.md` §3.6.5–§3.6.7, §4–§6 to the shipped OPDS (Download folder, Filename format, Calibre hint, credentials rule, web password semantics), KOReader (upload XPath derivation, Basic auth, Smart-sync alternate-id probe and ≤0.1-point rule, Document Matching, Send Metadata, credentials prerequisite), reader menu contents/order/popups, QR of page text, bookmark creation and deletion flows, footnote entry points, chapter skip opt-in and `.fb2`/`.md` formats per FR-081, FR-084..FR-088, FR-160..FR-164, FR-169, FR-172..FR-175 (contradicts)
- [X] T163 Update `README.md` (formats incl. `.fb2` *(fork-only)* and `.md`, USB Drive boards, tilt boards, 34 languages, papyrix note) and `ROADMAP.md` (Transparent sleep screens shipped) per FR-022, FR-025, FR-074, FR-138, FR-142, FR-158 (contradicts)
- [X] T164 Update `docs/file-formats.md`: `EXPECTED_VERSION 45`, v44 per-page link records, TextBlock ruby strings, `RUBY_CONTINUE=64`, and document `css_rules.cache` v12, `html/`, `.part` partial sections with the watermark trailer, `img_*`/`.pxc`, covers/thumbnails, TXT `index.bin` v3, XTC and FB2 caches, progress files and the JSON stores (`settings`, `state`, `recent`, `opds`, `wifi`, `koreader`, bookmarks) per contracts/cache-formats.md and FR-047, FR-050, FR-052..FR-054, FR-072, FR-076, FR-096, FR-101, FR-103..FR-106 (contradicts)
- [X] T165 Update `docs/webserver.md` and `docs/webserver-endpoints.md`: uploads never overwrite (browser suffixes " (2)"), `/api/status` `serial` and device names, WebSocket `ERROR:File already exists` and zero-size `DONE`, USB Drive mode, dot-prefixed names inaccessible regardless of Show Hidden Files, WebDAV normalisation and NUL rejection per contracts/web-server.md and FR-145, FR-150..FR-156 (contradicts)
- [X] T166 Update `docs/sd-card-fonts.md` (Text Settings Font/Size tabs, group screen, Download All/Update All, delete flow, basename rule, 31-byte name limit) and `docs/dictionary.md` (ASCII-only case folding, unusable `.sidx` semantics and popup, 64 KB cap, 255-byte headwords, English stemmer, `/.dictionaries` in USER_GUIDE) per FR-115, FR-119, FR-120, FR-177, FR-181 (contradicts)
- [X] T167 Update `docs/activity-manager.md`, `docs/contributing/architecture.md`, `development-workflow.md`, `testing-debugging.md`, `getting-started.md`, `docs/i18n.md`, `test/README` and `test/corpus/README.md`: multi-core render task pinning, remaining `std::function` handlers, removed `ActivityWithSubactivity.h`, multi-board targets and FB2 flow, full persisted-areas list, fork `master` branch model and merge gates, `bin/run-tests`/`--asan`/pre-push hook/`pio run -t unit-tests`, 34 languages and generator rules, html-corpus expat note (or remove after T147), stale `docs/comparison.md` per FR-018, FR-142, FR-184, FR-192 and Constitution VII (contradicts)
- [X] T168 Update `AGENTS.md` (and the tracked `CLAUDE.md` pointer where it repeats it): `gh_release` `LOG_LEVEL=1`, the S3 environments, `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` location, `src/network/html/` sources, the full HAL table, `HalFile` lock-free accessors, current `GfxRenderer.cpp` malloc lines, cache versions 10/45, strip grayscale vs `storeBwBuffer`, and the CI table (`pr-formatting-check.yml` checks titles; `ci.yml` runs format, cppcheck, build and unit tests) per FR-052, FR-111, FR-183, FR-191 (contradicts)
- [X] T169 Correct stale source comments: `src/network/HttpDownloader.h` (CA verification claim), `lib/hal/HalDisplay.h` (unmeasured "1720ms"), `src/network/FirmwareFlasher.h`, `OtaBootSwitch.h` (OTA path, `patch_firmware_image.py`), `src/activities/settings/SdFirmwareUpdateActivity.h` (Arduino Update), `lib/KOReaderSync/KOReaderSyncClient.h` (default server), `src/activities/network/NetworkModeSelectionActivity.h`, `src/network/CrossPointWebServer.cpp` (FilesPage headers), `src/activities/settings/KOReaderSettingsActivity.h`, `src/activities/reader/KOReaderSyncActivity.h`, `lib/EpdFont/SdCardFontRegistry.h`, `lib/Xtc/README`, `lib/Xtc/Xtc/XtcParser.h`, `lib/Xtc/Xtc.cpp`, `lib/Xtc/Xtc/XtcTypes.h`, `src/activities/util/FrontlightPanelActivity.h`, `lib/hal/HalGPIO.h` (X4-only pin macros) and `STR_RECOVERY_MODE_HINT` in `lib/I18n/translations/english.yaml` per Constitution IV (contradicts)
- [X] T170 Strengthen test infrastructure: add a touch/home-key `BoardConfig` stub variant to `test/persistable_stores/` so pruned-entry persistence is pinned on both board classes; pin `sticky` and `papermono` asset selection in `test/ota_asset_selection/`; align the CI `unit-tests` configure step with `bin/run-tests` (PRE_TEST discovery, `-k 0`); record the external inputs (source book, pyphen, Pillow, fonts) for `test/hyphenation_eval/resources/generate_hyphenation_test_data.py` and `scripts/generate_test_epub.py` per Constitution IV, V and FR-192 (partial)
- [X] T171 Handle `.md` consistently with `.txt`: `src/util/BookCacheUtils.cpp` `clearBookCache`, `src/activities/boot_sleep/SleepActivity.cpp` cover lookup and `src/RecentBooksStore.cpp` title derivation, or route `.md` explicitly per FR-074, FR-097, FR-106 (partial)

### LOW

- [X] T172 Remove or wire dead code: `RecentBooksStore::getDataFromBook`, `RoundedRaffTheme::homeMenuShowsContinueReading`, `UDP_PORTS[]` in `src/network/CrossPointWebServer.cpp`, `OpdsParser::getBooks` copy, `KOReaderCredentialStore::clearCredentials` (no UI), `V1_LANGUAGES` from `scripts/gen_i18n.py`, the `frontButtonLayout` field/enum in `src/CrossPointSettings.h` per plan.md (unrequested)
- [X] T173 Add a non-value-initialising array variant to `lib/Memory/Memory.h` for large buffers the caller overwrites, and use it for the framebuffer-sized and decoder buffers per Constitution II (partial)
- [X] T174 Inject the `<version>-dev-<branch>-<sha>` string for every development environment (x4pro, x4c, papermono), not only `default` and `sticky`, in `scripts/git_branch.py` per FR-191 (partial)

---

## Phase 21: Convergence

**Purpose**: Remaining work found by assessing the code against spec.md, plan.md and the constitution on 2026-09-16, after Phase 20 was implemented. Ordered CRITICAL → LOW. Constitution violations first. Several items finish work a Phase 20 task claimed: those tasks stay `[X]` (task history is append-only) and the unlanded remainder is restated here.

### CRITICAL — Constitution V (defects pinned by enabled tests) and VI (no UB on untrusted input)

- [X] T175 CRITICAL: Guard the two empty-string `std::string::back()` defects and flip their skipped tests into enabled regression guards in the same commit — add an empty check to the `sortFileList` comparator in `lib/FsHelpers/FsHelpers.cpp:144-145` and to `getFileIcon` in `src/components/UIThemeUtils.h:23`, then replace the unconditional `GTEST_SKIP()` bodies of `TEST(SortFileList, EmptyEntryName)` and `TEST(FileIcon, EmptyName)` in `test/library_helpers/LibraryHelpersTest.cpp:273-276,557-560` with assertions shown to fail against the unguarded code. These are the only two unconditional skips in the tree, so neither defect is pinned today. `src/RecentBooksStore.cpp:29` bounds `path` by length but never by emptiness, so a corrupt `recent.json` carrying `"path": ""` reaches the `getFileIcon` UB through `src/activities/home/RecentBooksActivity.cpp:42` per Constitution V, Constitution VI and FR-036 (contradicts)
- [X] T176 CRITICAL: Bound the XTH 2-bit plane reads on the cover and reader page paths, which index `colIndex * ((h+7)/8) + byteInCol` into a buffer sized `(w*h+7)/8` and therefore read past it whenever an XTC page height is not a multiple of 8 — add the `byteOffset < planeSize` guard that `lib/Xtc/Xtc.cpp:453-455` already applies on the thumbnail path to the cover path at `lib/Xtc/Xtc.cpp:247-249` and to `getPixelValue` at `src/activities/reader/XtcReaderActivity.cpp:170-172`, or reject the shape in `XtcParser::validatePageTable` (`lib/Xtc/Xtc/XtcParser.cpp:214-231`) where width/height are taken verbatim from the file at `:258-262`; extend `test/xtc_fb2_readers/XtcCoverThumbTest.cpp:684-688` to pin the cover and reader paths, not only the thumbnail, per Constitution VI and SC-004 (contradicts)
- [X] T177 CRITICAL: Honour every `serialization::readPod`/`readString` return value on the section-cache deserialization path so a truncated cache fails deterministically instead of branching on indeterminate values — zero-initialise and check `w`/`h` and both `readString` calls in `ImageBlock::deserialize` (`lib/Epub/Epub/blocks/ImageBlock.cpp:437-446`), check `wc`, `hasFocus` and `textBytes` in `TextBlock::deserialize` (`lib/Epub/Epub/blocks/TextBlock.cpp:327-333`) before the sanity checks at `:337-344`, and check the element tag read at `lib/Epub/Epub/Page.cpp:241-242` and the `xPos`/`yPos` reads in `PageLine::deserialize` at `lib/Epub/Epub/Page.cpp:44-47`, returning `nullptr` with a `LOG_ERR` in each case; re-sync the mirrored stub at `test/epub_section/SectionLinkStubs.cpp:34-45` so it reproduces the new failure contract per Constitution VI, T143 and FR-052 (partial)

### CRITICAL — Constitution II (no bare `new` for fallible allocations, no oversized stack frames)

- [X] T178 CRITICAL: Replace the three bare `new` expressions still on fallible paths, which `-fno-exceptions` turns into `abort()` on OOM — `lib/Epub/Epub/Page.cpp:222` (`std::unique_ptr<Page>(new Page())`, called per cached page; callers at `lib/Epub/Epub/Section.cpp:768,819` and `lib/Fb2/Fb2/Fb2Section.cpp:228` already handle `nullptr`), `src/activities/reader/EpubReaderActivity.cpp:1160` (`std::unique_ptr<Section>(new Section(...))`, dereferenced three lines later at `:1163`) and `src/network/CrossPointWebServer.cpp:187` (`server->addHandler(new WebDAVHandler())`, where `new (std::nothrow)` is correct because `addHandler` takes ownership); add the `LOG_ERR` + null branch in each case per Constitution II, FR-190 and T146 (contradicts)
- [X] T179 CRITICAL: Move the remaining over-256-byte stack buffers in `src/network/CrossPointWebServer.cpp` off the task stack, mirroring the conversion already landed in `src/network/WebDAVHandler.cpp:687` — the 4 KB `uint8_t buffer[chunkSize]` in the download handler at `:588-589`, the `char name[500]` in `scanFiles` at `:445` (hoist one heap allocation before the `while (file)` loop) and the three `char output[512]` at `:507`, `:1140` and `:1333`; use `makeUniqueNoThrowForOverwrite<uint8_t[]>` / `<char[]>` with a null check and `LOG_ERR`, and name the sizes as `constexpr` alongside the existing style at `src/network/WebDAVHandler.cpp:22-24` per Constitution II, T153 and plan.md Complexity Tracking row 3 (partial)

### HIGH

- [X] T180 Fix the reader toolbar OOM handler, which logs "falling back to the list menu" and then calls `openReaderMenu()` — itself — so an OOM re-enters the same `usesToolbarMenu()` branch and recurses until the stack overflows: at `src/activities/reader/EpubReaderActivity.cpp:252-256` reset `overlay = Overlay::None` and fall through to the list-menu path at `:262` (or return without opening a menu, as `openOverlay()` does at `:1858-1864`), never leaving `overlay == Overlay::Toolbar` with `toolbarUi` null since `render`/`loop` test that pair at `:1423` and `:426`, and correct the `LOG_ERR` text to state the behaviour actually taken per FR-081 and Constitution IV (contradicts)
- [X] T181 Wire the ditherer `valid()` gate into the two stream converters so a failed error-row allocation degrades to plain quantisation instead of dereferencing null — after each `makeUniqueNoThrow` ditherer construction in `lib/PngToBmpConverter/PngToBmpConverter.cpp:658-676` and `lib/JpegToBmpConverter/JpegToBmpConverter.cpp:681-698`, add the `if (ptr && !ptr->valid()) { LOG_ERR(...); ptr.reset(); }` check that `lib/GfxRenderer/Bitmap.cpp:176-190` already performs; `valid()` exists on all three classes (`lib/GfxRenderer/BitmapHelpers.h:35,122,228`) and the row loops already fall back when the pointer is null, so covers and thumbnails still render, only undithered per FR-046, FR-031 and T146 (partial)
- [X] T182 Route the eleven hardcoded English error strings in `src/activities/settings/FontDownloadActivity.cpp` through `tr()` — they are painted on screen by `renderer.drawCenteredText(...)` at `:840`, so they are user-facing text, not log output: add seven fixed `STR_*` keys plus four `%s`-format keys to `lib/I18n/translations/english.yaml`, regenerate with `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`, replace the literal assignments at `:144`, `:154`, `:165`, `:173`, `:176`, `:402` and `:548`, and build the four filename-bearing messages at `:469`, `:481`, `:491` and `:503` into a fixed stack buffer with `snprintf` per FR-142 and the constitution's i18n rule (contradicts)
- [X] T183 Remove the per-render `std::string` / Arduino `String` construction from the hot paths T152 names but did not change: the status-bar title in `src/activities/reader/EpubReaderActivity.cpp:1714-1745` (plus the by-value `TocEntry` copy at `:1735` and the `std::string title` parameter of `drawStatusBar` at `src/components/themes/BaseTheme.h:276-279` and its Txt/Xtc/Fb2 callers), the `.pxc` cache path rebuilt per pass by `getCachePath` in `lib/Epub/Epub/blocks/ImageBlock.cpp:37-43` (called from `render` at `:360` and `hasValidCache` at `:300` — derive it once in the constructor or fill a caller-owned buffer), the per-repaint temporaries in `src/activities/util/KeyboardEntryActivity.cpp:711,753,762-772,792-799`, the two Arduino `String`s in `src/activities/settings/ClearCacheActivity.cpp:116-120`, and the `String` concatenation building the WebSocket `PROGRESS` message at `src/network/CrossPointWebServer.cpp:1733` per Constitution II and T152 (partial)

### MEDIUM

- [X] T184 Finish T156's `std::function` removals using the in-tree function-pointer + context pattern (`src/components/themes/BaseTheme.h:139-149`, `src/network/HttpDownloader.cpp:40-45`): `ReaderToolbarUi::Model`'s `rowText`/`rowValue` at `src/activities/reader/ReaderToolbarUi.h:39-40` (with the four capturing lambdas at `src/activities/reader/EpubReaderActivity.cpp:1988-2002` and the `windowLabels_`/`windowValues_` members at `ReaderToolbarUi.h:93-95`), the `popupFn` parameters at `lib/Epub/Epub/Section.h:90,97` and `lib/Fb2/Fb2/Fb2Section.h:42`, the `completePageFn`/`popupFn` members at `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h:30-31,173-176` and `lib/Fb2/Fb2/Fb2SectionParser.h:27-28,76-77`, the XTC callback at `lib/Xtc/Xtc/XtcParser.h:69` and `lib/Xtc/Xtc.h:93` (also stopping the by-value pass), and `CrossPointWebServer::scanFiles` at `src/network/CrossPointWebServer.h:87` (which additionally copies `FileInfo` and its Arduino `String` per entry); update the call sites including `test/xtc_parser/XtcParserTest.cpp:99,122,205` per Constitution II, T156 and plan.md Complexity Tracking row 1 (partial)
- [X] T185 Finish T157 by routing the pure-logic translation units it names through `lib/Platform/PlatformSeam.h` — replace `ESP.getFreeHeap()`/`ESP.getMaxAllocHeap()`/`millis()` with `platform::freeHeap()`/`platform::maxAllocHeap()`/`platform::millis()` in `lib/Epub/Epub/css/CssParser.cpp:977,981`, `lib/EpdFont/FontDecompressor.cpp:78,82,86`, `lib/EpdFont/SdCardFont.cpp:133,870,898,1008,1082,1095,1228,1338,1601,1632`, `lib/GfxRenderer/GfxRenderer.cpp:1650,1705`, `src/util/Dictionary.cpp:237,282,515` and `src/util/ButtonNavigator.cpp:65,73`; replace the `vTaskDelay(1)` at `lib/PngToBmpConverter/PngToBmpConverter.cpp:81` with `platform::yield()` and drop its FreeRTOS includes; drop the now-unneeded `#include <Arduino.h>` from each; and add the `Print` sink and string aliases the task also calls for so `lib/OpdsParser/OpdsParser.h:2` and `lib/FsHelpers/FsHelpers.h:2` stop including Arduino headers, retiring the matching per-suite stubs per Constitution III, T157 and plan.md Complexity Tracking row 4 (partial)
- [X] T186 Rewrite the protected-path documentation to match the hardening that landed in T145 — `docs/webserver.md` (lines 19-25 and the Security Notes bullet at 196-199) and `docs/webserver-endpoints.md` (lines 100-107, the `GET /download` note at 126-128 and the `/delete` failure-reason list at 251) still describe the pre-hardening behaviour: state that `/api/files`, `/download`, `/delete`, `/upload` and `/mkdir` reject a dot-prefixed or protected component anywhere in the normalised path, that `/upload` and `/mkdir` additionally reject empty names and names containing a separator via `WebPathUtils::checkItemName`, and delete both the claim that `/upload` and `/mkdir` do not screen the name they are given and the claim that a normally-named file inside a hidden folder is reachable per FR-150..FR-156 and T165 (contradicts)
- [X] T187 Reword `STR_RECOVERY_MODE_HINT` at `lib/I18n/translations/english.yaml:465`, which still tells users to place `firmware.bin` at the SD card root although the shipped screen is a picker that takes any firmware `.bin` from anywhere on the card (and S3 release assets are `firmware-<board>.bin` per `src/network/FirmwareBoardTag.cpp:10`); keep it within the single UI_10 line drawn at `src/activities/settings/SdFirmwareUpdateActivity.cpp:264`, regenerate the tables, and either drop or update the key in the other 33 catalogues so they fall back to English; commit only the YAML sources per Constitution IV, FR-167 and T169 (missing)

### LOW

- [X] T188 Correct the two factual errors and five stale line anchors in `AGENTS.md` (`CLAUDE.md` is a symlink to it — edit `AGENTS.md` only): the Error Handling item at line 335 claims the one in-tree `assert(false)` is the missing framebuffer in `GfxRenderer::begin()`, but `src/activities/ActivityManager.h:75` carries a second, and the citation should be `lib/GfxRenderer/GfxRenderer.cpp:133`; re-resolve the anchors at lines 215 (`lib/hal/HalStorage.cpp:271-273,286-288,305-306`), 216 (`lib/hal/HalStorage.cpp:247-251`), 335, 336 (`lib/GfxRenderer/GfxRenderer.cpp:172-180`) and 544 (`src/main.cpp:593`), which drifted during Phase 20 itself per Constitution IV and T168 (contradicts)
- [X] T189 Update `docs/activity-manager.md:247`, which still lists the theme row/label callbacks at `src/components/themes/BaseTheme.h:244-247` among the remaining `std::function` users although T156 converted them — drop that clause, correct the `SettingInfo` citation to `src/activities/settings/SettingsActivity.h:56-59`, and point the "prefer a plain function pointer" guidance at `src/components/themes/BaseTheme.h:139-149` as the in-tree example of the replacement pattern per Constitution IV and T167 (contradicts)
- [X] T190 Reconcile the retrospective artifacts with the test program Phase 20 actually shipped: the tree now registers 59 suites (`test/CMakeLists.txt`, one `crosspoint_suite(...)` per directory) and CI runs both the plain and the ASan jobs (`.github/workflows/ci.yml:162-174`), but `spec.md:569` (FR-192), `plan.md:21,31,41,135` and `quickstart.md` still say 43 suites, and the Principle V row at `plan.md:43` still reads PARTIAL and lists modules (EPUB container/OPF/nav/NCX parsers, `Epub.cpp`, TXT pagination, `Bitmap.cpp`, `UrlUtils`, `FontInstaller`, `KOReaderSyncClient`, `KOReaderDocumentId`, `DictionaryRegistry`, `FirmwareFlasher`, `OtaBootSwitch`, `MappedInputManager`) that now have suites, plus a "CI runs the plain build only; the ASan gate is manual" clause that is no longer true per FR-192 and plan.md Constitution Check row V (contradicts)
