# Implementation Plan: CrossPoint Reader Firmware (Retrospective Baseline)

**Branch**: `001-crosspoint-reader-baseline` | **Date**: 2026-09-15 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-crosspoint-reader-baseline/spec.md`

**Note**: This plan is retrospective. It records the architecture the shipped firmware actually has, in the form `/speckit-plan` would have produced, so that later features can be planned against it and `/speckit-converge` can measure the code against a stated intent.

## Summary

CrossPoint Reader is an EPUB-first e-book reader firmware for ESP32 e-ink devices. The primary requirement is a focused reading experience on a 380 KB-RAM, single-core microcontroller with a slow 800×480 e-ink panel and an SD card as the only large storage. The technical approach that satisfies it is an *SD-first* design: every expensive result (book metadata, parsed CSS, laid-out pages, decoded images, covers, progress) is streamed once from the EPUB ZIP through bounded parsers into versioned binary caches on the card, and the runtime only ever holds one page's worth of layout, one 48 KB framebuffer and a handful of glyphs in RAM. Screens are Activities driven by a single-threaded state machine with one render task; every hardware dependency sits behind `lib/hal` over the FreeInk SDK; every untrusted byte (SD caches, ZIP headers, fonts, JSON, feeds, xpointers) is length-checked against physical file sizes before it drives an allocation. Wireless features (web file transfer, OPDS, OTA, KOReader sync, font download) are on-demand activities that release caches before allocating TLS and reboot silently afterwards to defragment the heap.

## Technical Context

**Language/Version**: C++20 (`-std=gnu++2a`), no exceptions, no RTTI; Python 3.8+ for build/generator scripts; JavaScript (browser) for the web UI.

**Primary Dependencies**: Arduino-ESP32 via pioarduino platform-espressif32 55.03.37; FreeInk SDK (git submodule: BoardConfig, FreeInkDisplay, InputManager, SDCardManager/SdmmcBlockDevice, UsbMassStorage, PowerManager, FrontlightManager, Rtc, Imu, SecureNet/wolfSSL, FreeInkUI, Icons); vendored expat (`XML_GE=0`, `XML_CONTEXT_BYTES=1024`), miniz (inflate), uzlib; registry libs ArduinoJson 7.4.2, QRCode 0.0.1, PNGdec 1.1.6, JPEGDEC (pinned commit, patched), WebSockets 2.7.3, Arduino-wolfSSL 5.7.2 (SP ECC, `FP_MAX_BITS` 8192, 2 KB max fragment).

**Storage**: SD card (FAT, UTF-8 long names) under `/.crosspoint/` for all firmware state and per-book caches; RTC_NOINIT memory for reboot-surviving flags and the crash capture; NVS only for the SDK's panel fingerprint. SPIFFS is reserved but never mounted.

**Testing**: Host GoogleTest program under `test/` (43 suites, ~1,003 tests) built with CMake/Ninja via `bin/run-tests` (plain and `--asan`), malformed-input corpus under `test/corpus/`, deterministic fixture generators under `scripts/generate_test_*.py`; `pio check` (cppcheck low/medium/high); `./bin/clang-format-fix -c`; on-device verification with `scripts/debugging_monitor.py`; the official simulator (sister repo, per-developer `platformio.local.ini`).

**Target Platform**: ESP32-C3 (Xteink X4, X3; one combined binary with runtime detection) and ESP32-S3 (Seeed reTerminal Sticky; Xteink X4 Pro and X4 Classic and M5 Paper Mono with PSRAM, native SDMMC and USB mass storage). 800×480 (X4) / 792×528 (X3) e-ink, 16 MB flash with two 6.25 MB OTA slots.

**Project Type**: Embedded firmware (single PlatformIO project) with an in-repo host test program and a browser-side web UI compiled into flash.

**Performance Goals**: Cached page turn = one fast partial refresh plus the render time of one page; first page of an uncached chapter visible before layout of the rest completes (incremental build); anti-aliased text without a second framebuffer; a Wi-Fi/TLS session must fit in the heap a reading session leaves (heap gates 35–40 KB free / 20 KB largest block). Wall-clock numbers are not asserted; allocation counts are (Constitution IV).

**Constraints**: ~380 KB usable RAM, no PSRAM on the C3; single 48,000-byte framebuffer (`EINK_DISPLAY_SINGLE_BUFFER_MODE`); single core; 8 KB render-task stack; e-ink refresh 1–2 s for a full update; SdFat is not thread-safe (one storage mutex); `-fno-exceptions` makes bare `new` abort; all SD and network input is hostile.

**Scale/Scope**: ~45 k lines in `src/`, ~37 k in `lib/Epub`, ~60 screens (activities), 205 functional requirements across 18 areas, 34 UI languages, 5 board environments, 43 host test suites.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status for this baseline | Evidence / notes |
|-----------|--------------------------|------------------|
| I. A Focused Reading Device | PASS | Every capability in spec.md serves reading, library management, typography, local transfer or maintainability. Out-of-scope items (apps, authoring, RSS/browsers, PDF) are absent. The only "connector" surfaces are the pre-existing OPDS and KOSync ones that SCOPE.md grandfathers. |
| II. Memory Is the Design Constraint | PASS with recorded exceptions | SD-first caching, streaming parsers, bounded arenas, nothrow helpers (`lib/Memory/Memory.h`), framebuffer lending (`lib/Memory/BuildScratch.h`) and heap gates are the design. Known deviations (bare `new`/`make_unique` on fallible paths, some >256-byte stack buffers, per-render `std::string`/`std::function` use, un-debounced SD writes) are listed in Complexity Tracking and become convergence tasks. |
| III. Portability Behind the HAL | PASS with recorded exceptions | Device code lives in `lib/hal` and the SDK; readers, parsers, caches and typography are host-compiled by 43 suites. Some pure-logic libraries still include Arduino headers (`Print`, `WString`, `ESP.*`, `millis()`, FreeRTOS) and need per-suite stubs; listed in Complexity Tracking. |
| IV. Evidence Over Claims | PASS | Performance rationale in `research.md` cites mechanisms recorded in code comments and commit history (allocation counts, IRAM/DRAM placement, SD transaction counts). Two unmeasured claims in headers are recorded as drift. |
| V. Tests Prove Behavior (NON-NEGOTIABLE) | PARTIAL | 43 mutation-verified suites cover the parsers, caches, layout, fonts, sync mapping, dictionary, stores and web paths. Host-reachable logic without a suite (EPUB container/OPF/nav/NCX parsers, `Epub.cpp` orchestration, TXT pagination, `Bitmap.cpp`, `UrlUtils`, `FontInstaller`, `KOReaderSyncClient`, `KOReaderDocumentId`, `DictionaryRegistry`, `FirmwareFlasher`, `OtaBootSwitch`, `MappedInputManager`, and others) is the largest convergence gap. CI runs the plain build only; the ASan gate is manual. |
| VI. Untrusted Input Is Hostile | PASS with recorded exceptions | Length/count validation before allocation exists for book.bin, section.bin, FB2 caches, ZIP, JPEG/PNG, XTC, dictzip, release JSON, xpointers, `.cpfont` headers and credential stores. Gaps: TXT `index.bin` page count, `.cpfont` glyph records vs bitmap bounds, `PageImage` null image block, unbounded JSON string fields in `opds.json`/`recent.json`/KOSync responses, unvalidated upload paths. No corpus for container/OPF/nav/NCX or TXT. |
| VII. Upstream-First Fork Hygiene | PASS | Fork `master` carries upstream plus one-defect-one-commit fixes and fork-only test tooling; this feature's artifacts live under `specs/` and `.specify/` only. Upstream PR branches are cut from `origin/develop`. |

**Gate result**: proceed. Principle V and the recorded II/III/VI exceptions are not blockers for a retrospective baseline; they define the remaining work that `/speckit-converge` appends to `tasks.md`.

## Project Structure

### Documentation (this feature)

```text
specs/001-crosspoint-reader-baseline/
├── plan.md              # This file
├── research.md          # Design decisions, provenance, documentation drift and limitation registers
├── data-model.md        # Entities, persisted formats, state machines
├── quickstart.md        # Build, flash, test and per-story verification
├── contracts/           # External and on-card interface contracts
│   ├── sd-card-layout.md
│   ├── cache-formats.md
│   ├── settings-and-input.md
│   ├── web-server.md
│   ├── opds-feed.md
│   ├── kosync-protocol.md
│   ├── firmware-update.md
│   ├── cpfont-and-manifest.md
│   ├── xtc-format.md
│   └── dictionary-stardict.md
└── tasks.md             # Retrospective task breakdown (+ convergence phases)
```

### Source Code (repository root)

```text
src/
├── main.cpp                         # boot classification, main loop, sleep/wake, power button, screenshots
├── CrossPointSettings.{h,cpp}       # settings singleton (SETTINGS), migrations, StatusBarSpec, ReaderRenderSpec
├── SettingsList.h                   # the single settings table (device UI + JSON + web API)
├── CrossPointState.{h,cpp}          # runtime state singleton (APP_STATE)
├── RecentBooksStore.*, OpdsServerStore.*, WifiCredentialStore.*   # JSON stores
├── MappedInputManager.*             # logical buttons, touch gestures, long-press policy
├── SdCardFontSystem.*, FontInstaller.*, ReaderFontSizes.*, fontIds.h
├── SilentRestart.h, BookmarkEntry.h
├── activities/
│   ├── Activity.*, ActivityManager.*, ActivityResult.h, RenderLock.h
│   ├── UiListActivity.*, UiTabListActivity.*        # FreeInkUI list bases
│   ├── boot_sleep/    BootActivity, SleepActivity
│   ├── home/          HomeActivity, FileBrowserActivity, RecentBooksActivity, CrashActivity
│   ├── reader/        ReaderActivity (base), EpubReaderActivity (+Menu, Bookmarks, ChapterSelection,
│   │                  Footnotes, PercentSelection, EndOfBookOptions, QrDisplay, ReaderToolbarUi),
│   │                  TxtReaderActivity, XtcReaderActivity(+ChapterSelection), Fb2ReaderActivity(+ChapterSelection),
│   │                  DictionaryWordSelectActivity, DictionaryDefinitionActivity, KOReaderSyncActivity,
│   │                  ProgressFile.h, ReaderUtils.h, EpubReaderUtils.h
│   ├── settings/      SettingsActivity, TextSettingsActivity(+Preview), StatusBarSettingsActivity,
│   │                  ButtonRemapActivity, ClockOffsetActivity, ClockSyncActivity, KeyboardLayoutsActivity,
│   │                  LanguageSelectActivity, ClearCacheActivity, FontDownloadActivity,
│   │                  OpdsServerListActivity, OpdsSettingsActivity, KOReaderSettingsActivity,
│   │                  KOReaderAuthActivity, OtaUpdateActivity, SdFirmwareUpdateActivity
│   ├── network/       NetworkModeSelectionActivity, WifiSelectionActivity, CrossPointWebServerActivity,
│   │                  CalibreConnectActivity, UsbDriveActivity
│   ├── browser/       OpdsBookBrowserActivity
│   └── util/          KeyboardEntryActivity(+LayoutSet), BmpViewerActivity, ConfirmationActivity,
│                      FullScreenMessageActivity, FrontlightPanelActivity, IntervalSelectionActivity
├── components/        UITheme, UIThemeTokens, UIScale, UiAppHost, UiAppHelpers, OptionPopup, UiSliderDialog,
│                      themes/{BaseTheme, lyra/LyraTheme, lyra/Lyra3CoversTheme, roundedraff/RoundedRaffTheme}, icons/
├── network/           CrossPointWebServer, WebDAVHandler, HttpDownloader, OtaUpdater, OtaBootSwitch,
│                      FirmwareFlasher, FirmwareBoardTag, html/{HomePage,FilesPage,SettingsPage,FontsPage}.html
├── util/              Dictionary, DictZip, DictHtmlPages, DictionaryRegistry, HtmlToPlainText, BookmarkFile,
│                      BookmarkUtil, NextBookFinder, BookCacheUtils, ButtonNavigator, QrUtils, ScreenshotUtil,
│                      StringUtils, UrlUtils, OpdsFilename, TaskWatchdog.h
└── platform/          UsbSerialJtagHandoff, skip_efuse_blk_check.c

lib/
├── hal/               HalStorage(+HalFile), HalDisplay, HalGPIO, HalPowerManager, HalSystem, HalClock,
│                      HalFrontlight, HalTiltSensor
├── Epub/              Epub (facade), Epub/{BookMetadataCache, Section, Page, ParsedText, ReaderRenderSpec,
│                      parsers/{Container,ContentOpf,TocNav,TocNcx,ChapterHtmlSlim}Parser, css/{CssParser,CssStyle},
│                      blocks/{TextBlock,ImageBlock,BlockStyle}, converters/{ImageDimsProbe, Jpeg/PngToFramebuffer,
│                      DirectPixelWriter, PixelCache, DitherUtils}, hyphenation/{Hyphenator, LiangHyphenation,
│                      LanguageRegistry, generated/*.trie.h}, htmlEntities, TokenBoundary, VisibleTextUtils}
├── Fb2/ (fork-only)   Fb2, Fb2/{MetadataParser, SectionParser, Section, CoverExtractor, XmlEncoding}
├── Xtc/               Xtc, Xtc/{XtcParser, XtcTypes}
├── Txt/               Txt
├── GfxRenderer/       GfxRenderer, FontCacheManager, Bitmap, BitmapHelpers
├── EpdFont/           EpdFont, EpdFontFamily, EpdFontData, FontDecompressor, SdCardFont, SdCardFontManager,
│                      SdCardFontRegistry, builtinFonts/ (generated), scripts/ (font converters, manifest)
├── KOReaderSync/      KOReaderSyncClient, KOReaderCredentialStore, KOReaderDocumentId, ProgressMapper,
│                      ChapterXPathResolver
├── OpdsParser/, JsonParser/ (StreamingJsonParser, ReleaseJsonParser), Serialization/ (PersistableStore,
│   Serialization.h, BufferedFile.h, ObfuscationUtils, CredentialIntegrity), ZipFile/, InflateReader/,
│   JpegToBmpConverter/, PngToBmpConverter/, MiniBidi/, Utf8/, I18n/ (+translations/*.yaml), Memory/,
│   Logging/, FsHelpers/, XmlParserUtils/, expat/, miniz/, uzlib/

test/                  43 gtest suites + corpus/ + support/ + fixtures (see research.md §Test program)
scripts/               build_html.py, gen_i18n.py, git_branch.py, patch_*.py, generate_test_*.py, debugging_monitor.py
bin/                   run-tests, install-hooks, clang-format-fix
freeink-sdk/           hardware SDK submodule
platformio.ini, partitions.csv, sdkconfig.defaults
```

**Structure Decision**: Single embedded project. Reader-core logic (parsers, layout, caches, typography, sync mapping, dictionary, stores) lives in `lib/` and `src/util/` and is host-compilable; screens live in `src/activities/`; hardware access is confined to `lib/hal` and the SDK. The host test program mirrors this split: each suite compiles the production `.cpp` files it covers against per-suite stub headers for the HAL seam.

## Retrospective Phases (how the shipped system decomposes)

The task breakdown in `tasks.md` follows these phases; each maps to the user stories in `spec.md`.

1. **Setup** — PlatformIO environments per board, partition layout, pre-build generators (HTML, i18n, version, library patches), formatting and static-analysis tooling.
2. **Foundational** — HAL (storage mutex, display, GPIO, power, system, clock, frontlight, tilt), memory helpers, logging and crash capture, serialization and JSON stores, settings model and table, activity manager and render lock, renderer and built-in fonts, theme system and FreeInkUI hosting, input mapping, i18n tables.
3. **US1 Read an EPUB** — ZIP reader, container/OPF/TOC parsers, metadata cache, CSS parser and cache, chapter HTML parser, text layout and hyphenation, section cache with incremental builds, image pipeline, `ReaderActivity`/`EpubReaderActivity`, progress persistence.
4. **US2 Library** — Home, file browser, recent books, next-book finder, cover thumbnails, image viewer.
5. **US3 Boot/sleep** — boot classification, sleep screens, quick resume, wake verification, crash-loop guard, recovery mode.
6. **US4 Navigation** — reader menu, chapter selection, footnotes and links, bookmarks, go-to-percent, end of book.
7. **US5 Typography & display** — text settings with preview, orientation, anti-aliased grayscale, status bar settings, refresh cadence, night mode, themes.
8. **US6 Other formats** — TXT reader, XTC parser/reader, FB2 library/reader.
9. **US7 SD fonts & scripts** — `.cpfont` loader, registry/manager, CJK fallback, bidi/shaping, font download, web font API.
10. **US8 File transfer** — Wi-Fi selection and credential store, web server and pages, HTTP API, WebSocket upload, WebDAV, Calibre mode, hotspot, USB Drive, HTTP client.
11. **US9 OPDS** — server store and screens, feed parser, browser, download naming.
12. **US10 Controls** — remap wizard, power-button modes, tilt, control center, keyboard entry and layouts.
13. **US11 Localisation** — 34 translation catalogs, generator, language screen.
14. **US12 Dictionary** — StarDict reader, dictzip, sidecars, word selection, definition screens.
15. **US13 KOReader sync** — credential store, client, document id, xpointer generation/resolution, sync and auth screens.
16. **US14 Updates** — release JSON parser, OTA updater, board tag, SD flasher, otadata switch, recovery mode.
17. **US15 Conveniences** — auto page turn, screenshots, QR export.
18. **US16 Diagnostics** — crash report, serial commands, heap telemetry, host test program and CI.

## Complexity Tracking

> Recorded constitution deviations present in the baseline. Each is either justified here or becomes a convergence task.

| Violation | Why Needed / Why Present | Simpler Alternative Rejected Because |
|-----------|--------------------------|-------------------------------------|
| `std::function` for activity result handlers, theme drawing callbacks, toolbar row text, Section/XTC/FB2 callbacks (II) | Inherited upstream API shape; result handlers are set once per child activity; theme callbacks capture activity state. | Function-pointer + context structs are the documented preference; migration is mechanical but cross-cutting (upstream shape must be mirrored, VII). Recorded as convergence work, not a blocker. |
| `std::make_unique` / bare `new` on fallible paths (activities, Section, QR buffer, web server objects, ditherers, XtcParser) (II) | Historical; most sites are one-shot allocations at screen entry where OOM is already fatal in practice. | `makeUniqueNoThrow` exists and is used on newer paths; each remaining site is a one-line fix and is listed for convergence. |
| Stack buffers over 256 bytes in parsers/decoders (CSS 2.6 KB, Liang 1.4 KB, PNG decode 2.9 KB, ReleaseJsonParser 1.7 KB, dictionary 2 KB, paragraph streamers) (II) | They run on the 8 KB render task or the main task with measured headroom; heap alternatives fragment the heap the rule exists to protect. | Static or member storage is the alternative for the largest ones; recorded for case-by-case review. |
| Arduino headers in pure logic (`Print` sinks, `WString` overloads, `ESP.getFreeHeap`, `millis`, FreeRTOS delays) (III) | Streaming parsers are `Print` sinks by SDK convention; heap gates need the live allocator; host suites carry stubs. | A thin platform seam (`lib/Memory`/`lib/Logging`-style) would remove per-suite stubs; upstream has started this; mirror it when syncing. |
| Host-reachable modules without suites; CI without an ASan job (V) | The test program is fork-only and grew tranche by tranche; upstream added more suites after the last sync. | Not justified; this is the top convergence priority. |
| Two suites link the host system expat (`chapter_html_slim_parser`, `opds_parser`) | Upstream-era suites predate the in-tree expat target; one documents the divergence. | Switching both to the in-tree expat is small and listed for convergence. |
| No authentication/TLS on the web server; open hotspot; TLS peer verification disabled for outbound HTTPS | RAM budget and trusted-LAN assumption (documented in docs/webserver.md); certificate bundles do not fit the C3 heap with wolfSSL. | Recorded as an explicit assumption in spec.md; not a convergence task. |
