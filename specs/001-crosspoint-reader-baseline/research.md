# Research: CrossPoint Reader Firmware (Retrospective Baseline)

This file records the design decisions embodied in the shipped firmware, their rationale as stated in code comments, commit messages and issue references, the alternatives they replaced, and three registers that a later feature or convergence pass must consult: fork-vs-upstream provenance, documentation drift, and known limitations.

## 1. Design decisions

### Memory and storage strategy

- **Decision**: Stream every EPUB item through `Print` sinks (expat parsers, `HalFile`, header probes) and never load an item into RAM. **Rationale**: keeps the working set at 1–8 KB chunks on a ~380 KB heap; avoids decompress→temp→reread round trips. **Alternatives**: earlier temp-file round trips for NCX/nav; whole-image extraction for dimensions (kept only as fallback).
- **Decision**: Two-pass `book.bin` build through SD temp files with 4 KB buffered writers and one relocation pass. **Rationale**: unbuffered interleaved writes thrashed SdFat's single sector cache (31 s for a 1,732-spine omnibus); preloading all ZIP stats caused OOM (#134). **Alternatives**: in-RAM spine/TOC vectors.
- **Decision**: The section header is the cache key; the version byte is written last; builds commit by renaming a `.part` file. **Rationale**: any layout-affecting change invalidates automatically; a crash can never leave a version-valid incomplete file; the prior file stays readable during a rebuild.
- **Decision**: Incremental page-window builds with suspend-to-partial and byte-based page estimates. **Rationale**: giant single-spine books never finish laying out; users need the first page immediately and work must survive sleep/navigation.
- **Decision**: Persist inflated chapter HTML keyed by book, not by settings, in 8 KB chunks. **Rationale**: font/margin/orientation changes wipe `.bin` caches; skipping inflation is the main rebuild speed-up; 8 KB chunks cut SD writes 8×.
- **Decision**: `TextBlock` word data in one flat arena written verbatim (format v29); `ParsedText` tokens in `std::deque`. **Rationale**: ~250 throwing allocations per page load fragmented the heap; a CJK paragraph made `vector` reallocate 64–128 KB contiguous blocks that abort on a fragmented heap.
- **Decision**: CSS rules in bounded flat arrays with partial-cache retry and heap gates; byte-identical stylesheets de-duplicated by CRC32+size. **Rationale**: avoids `unordered_map` node allocations; converters emit one identical stylesheet per chapter.
- **Decision**: Images are header-probed at build time and extracted lazily on first render via a function-pointer hook; decoded pixels stream to a `.pxc` cache in ≤24 KB bands and replay from a heap-gated 16 KB-chunked RAM slot. **Rationale**: extracting every image stalled first open for seconds per image; a full-page 2-bpp image (~90 KB) does not fit beside the decoders; without a cache an image page re-decodes ~14 times (30 s freeze).
- **Decision**: Hyphenation tries are flash `constexpr` automata; Liang working buffers are fixed stack arrays. **Rationale**: thousands of small allocations per section fragmented the heap enough to block a 32 KB inflate buffer.
- **Decision**: Framebuffer storage is lent in place (`buildscratch`) to the inflate arena during builds. **Rationale**: free+realloc of the 48 KB framebuffer measurably decayed the max contiguous block over a session.
- **Decision**: Grayscale text on strip-capable panels renders 80-row bands into a scratch strip streamed to controller RAM; other panels snapshot the frame in 8,000-byte chunks. **Rationale**: no second 48 KB buffer; chunks avoid needing 48 KB contiguous.
- **Decision**: Built-in glyphs are DEFLATE-compressed in groups with per-page prewarm slots and a hot-group fallback; buffers are grow-only nothrow mallocs. **Rationale**: flash savings (#1056, #3144); a vector resize OOM on the render path aborted firmware in the field with ~11 KB free.
- **Decision**: SD fonts keep only interval tables, kern classes and ligatures resident; glyphs live in per-page mini arenas kept while heap ≥40 KB; the full kern matrix is never resident. **Rationale**: a Literata-class matrix is ~36–42 KB per style; realloc per page was the primary fragmenter.
- **Decision**: All SD access is serialized by one recursive mutex in `HalStorage`; `HalFile` closes under the lock. **Rationale**: SdFat's SPI state machine is not thread-safe (SdFat #518); recursive so an open out-param can be replaced under the lock (#2135, #2141).
- **Decision**: JSON stores share one non-template `PersistableStoreBase` owning the only ArduinoJson instantiation; reads never take the store mutex; legacy-shape upgrades go through `requestResave()` after unlock. **Rationale**: ~0.5 KB serializer clones per TU; SD write latency must not sit on the render path; saving inside `fromJson` would deadlock.
- **Decision**: Heap reclamation by rebuilding the Arduino core with a custom sdkconfig (Wi-Fi IRAM opt off, smaller timer stacks, cloud components removed), except on USB-OTG boards. **Rationale**: on the C3, IRAM and DRAM share SRAM, so ~25–30 KB of Wi-Fi IRAM and ~7 KB of oversized task stacks land in the heap (measured high-water marks); the custom rebuild omits TinyUSB MSC.
- **Decision**: wolfSSL tuned for low heap (single-precision ECC, `SP_SMALL`, `FP_MAX_BITS` 8192, 2 KB max fragment, TLS 1.3). **Rationale**: fast-math bignums OOM'd at the ~50 KB a reading session leaves; 2 KB records avoid a ~17 KB contiguous receive buffer.
- **Decision**: Silent restart (RTC_NOINIT magic + target) after every Wi-Fi session; touch boards stop Wi-Fi in place. **Rationale**: clears LWIP/TLS fragmentation without a splash; a software reset drops externally powered touch/frontlight rails.

### Reading position and navigation

- **Decision**: Visible-text codepoint offsets are the canonical position; page numbers and paragraph indexes are LUTs kept for fallback. **Rationale**: page indices no longer name the same content after a settings change; bookmarks and KOReader sync survive re-pagination.
- **Decision**: Progress is written to a temp file then renamed. **Rationale**: an interrupted truncate-in-place left a broken FAT cluster chain that stranded a book (#2275).
- **Decision**: Page turns fire on press when no long-press function is configured, on release/hold otherwise. **Rationale**: a hold cannot be distinguished from a tap if the action fires on press.
- **Decision**: KOReader xpointers are generated with expat (exact DOM indexing) but resolved with a hand-rolled tolerant byte streamer. **Rationale**: generation must match KOReader's indexing; resolution must never fail catastrophically on hostile remote strings on a tight heap. Upstream (#3174) is moving to a single expat resolver.
- **Decision**: KOSync accepts any 2xx and maps 204 to "not found". **Rationale**: Spring-based clones answer 201/204 where the reference server answers 200 (#2876).
- **Decision**: Default sync server moved to `sync.crosspointreader.com` with a `cfgVersion=2` migration pinning existing users to `sync.koreader.rocks`. **Rationale**: avoid silently moving accounts to a server where they do not exist.

### UI and input

- **Decision**: One shared 8 KB render task, one render mutex, deferred push/pop/replace (PR #1016). **Rationale**: per-activity render tasks cost 8 KB each; callbacks led to delete-this hazards.
- **Decision**: One settings table drives the device UI, JSON persistence and the web API; category-less entries are "persisted but hidden". **Rationale**: a setting declared once gets persistence and the web endpoint for free.
- **Decision**: Reader font size is a point size, not a Small/Medium/Large slot. **Rationale**: SD families ship arbitrary size sets.
- **Decision**: Input mapping uses the renderer's live orientation, not the persisted reader orientation. **Rationale**: Home/Settings render in portrait even when the reader preference is rotated.
- **Decision**: Long-press detection is one-shot with central release suppression. **Rationale**: a long-press action's release must not also fire the short-press action in the next activity.
- **Decision**: Themes are `constexpr` metrics tables plus a virtual base; one `FreeInkApp<24,6>` instantiation and a two-slot atomic token cell serve every screen. **Rationale**: flash-resident, zero runtime cost; capacity-templated classes minted separate copies per screen; per-app token copies cost ~1.5 KB each.
- **Decision**: Uploads never overwrite; the browser auto-suffixes " (2)". **Rationale**: server-side simplicity and no accidental data loss (docs still describe overwrite; see drift register).
- **Decision**: Web pages are gzip-compressed into PROGMEM at build time (not brotli). **Rationale**: Firefox refuses brotli over plain HTTP.

### Updates and diagnostics

- **Decision**: Panic message and a 1 KB stack window are captured by linker `--wrap` of `panic_abort`/`panic_print_backtrace` into RTC memory and dumped on the next boot; watchdog resets count as crashes only with the capture marker. **Rationale**: no USB needed for bug reports; plain WDT resets produced empty reports (#2830, #2968).
- **Decision**: The SD flasher writes the app partition raw and switches otadata directly; the eFuse block check is neutralised by a linker wrap. **Rationale**: the running IDF's `esp_image_verify` rejects the patched X3/X4 images with bogus eFuse-revision errors although the factory bootloader boots them.
- **Decision**: Per-board release assets; the C3 build keeps the historical `firmware.bin` name; a board tag is embedded in `.rodata` and scanned during OTA. **Rationale**: pre-existing releases only have `firmware.bin`; all S3 boards share a chip id, so the tag is the only way to tell them apart.
- **Decision**: Allocation counts, not wall-clock time, are the memory-behaviour proxy in tests; timing ceilings are opt-in (`CROSSPOINT_PERF_TIME=1`). **Rationale**: Constitution IV; timing is jittery and never a CI gate.

## 2. Provenance: fork vs upstream

Everything in `src/` and `lib/` is upstream work (crosspoint-reader/crosspoint-reader) except:

- **FB2 support** *(fork-only)*: `lib/Fb2/`, `Fb2ReaderActivity`, `Fb2ReaderChapterSelectionActivity`, FB2 branches in `ReaderActivity`, `SleepActivity`, `HomeActivity`, `FileBrowserActivity`, `NextBookFinder`, `UITheme`, `BookCacheUtils`, `RecentBooksStore`, `FsHelpers::hasFb2Extension` (commit `fc7f9407` plus cp1251/cp1252, top-level-section counting and notes-body fixes).
- **Hardening fixes** *(fork-only, one defect per commit, upstream-PR candidates)*: `book.bin`/`section.bin` truncation rejection; CSS BOM skip, quote-aware scanning, undefined-on-parse-failure; U+2011 semantics; hyphenation allocation reduction; ZIP entry-size validation, 256 KB RAM cap and byte-wise EOCD scan; FB2 cache bounds; `normalisePath` `.`-segment and backslash handling; WebDAV embedded-NUL rejection; XTC `chapterOffset` 4-byte read; PNG IHDR bit-depth validation; `ProgressMapper` digit overflow guard and comment/declaration skipping; `OtaUpdater` semver validation; `ReleaseJsonParser` size overflow; `StreamingJsonParser` closer-mismatch latch and overflow flag; bounded serialized strings.
- **Host test program** *(fork-only)*: 24 of the 43 suites, `test/corpus`, `test/support`, shared stub folders, `CROSSPOINT_SANITIZE`, `bin/run-tests`, `bin/install-hooks`, `.githooks/pre-push`, `scripts/register_unit_tests_target.py`, the libpcre3 CI fix. Upstream `develop` has since added suites the fork does not carry (absolute_grayscale, chapter_position, content_opf_parser, fs_helpers, inflate_stream, koreader_xpath_resolver, library_*, progress_comparison, sd_card_font).
- **Upstream changes not yet on the fork** (pinned by fork tests): release assets renamed to `crosspoint-<tag>-<device>.bin` (#3493); precise-position sync uploads (#3174) and mapped-position comparison (#3111).

## 3. Documentation drift register

Where the documentation describes the product differently from the code. The spec follows the code; each entry is a candidate documentation task.

### USER_GUIDE.md

- §1: screenshots go to `/screenshots/<title>/…` while a book is open, not only `screenshots/`; frontlight is not X4 Pro only (any `FREEINK_CAP_FRONTLIGHT` board; the double-click toggle is X4 Pro only).
- §2: "restarts reopen the last book" is conditional (sleep from reader, no crash pending, Back not held).
- §3.1: Home also has an OPDS Browser row, a Continue Reading tile, and Back = resume.
- §3.3: the full path is in a bottom band, not the header; brackets only in Classic/RoundedRaff; hidden dirs are hidden unless the toggle is on; `.png` also opens the viewer; **multi-select delete and rename/move do not exist on device**.
- §3.4: recents cap at 10, prune missing files and support removal.
- §3.5: four modes on USB-OTG boards (USB Drive); the signal indicator is bars, not dBm; the EPUB Optimizer runs in the browser, not on the device.
- §3.6.1: no "Status Bar" enum (it is a Customise Status Bar sub-screen); Refresh Frequency has "Never"; Night Mode, Restore Light on Wake and Quick Resume are Display rows; the §3.7 table omits Quick Resume.
- §3.6.2: font size is a point size (12/14/16/18 or family sizes), not Small/Medium/Large/X Large; Line Spacing has Extra Wide; Alignment has Book's Style; Images is Display/Placeholder/Suppress; the typography rows live in Text Settings; Manage Fonts is under Reader, not System.
- §3.6.3: "Long-press Chapter Skip" is "Long-press button behavior" with Off (default)/Chapter skip/Orientation change (no Page Scroll); Long-press Menu default is Disabled and has a Reader Menu option on home-key boards; Short Power Button Click has Confirm on touch builds; **"Quick-return from footnotes" is persisted and shown but read by no code**; Side Button Disabled, Touch Reader Controls, Show Reader Menu, Orient front buttons, Tilt and Short Back to File Browser rows are omitted.
- §3.6.4: Time to Sleep is any minute 1..30 plus Never; 34 languages (Bulgarian and Persian missing from the list); SD Card Firmware Update, Keyboard Layouts, Show Hidden Files, Clear Read Books, Move Finished Books rows omitted; recovery mode undocumented.
- §3.6.5/3.6.6: OPDS Download folder, Filename format and the Calibre hint are undocumented; credentials are sent only when both username and password are non-empty; the web API preserves a password only when the key is *omitted* (present-but-empty clears it).
- §3.6.7: the upload XPath is derived from the paragraph index or page fraction, not the recorded offset; requests also carry Basic auth with the plain password; Smart sync also probes the alternate document id and treats ≤0.1-point differences as synced; Document Matching and Send Document Metadata are undocumented; KOSync long-press does nothing without credentials.
- §4/§5: chapter skip is opt-in; the reader menu also has Bookmarks, Toggle Bookmark, Text Settings, Night Mode, Frontlight; Footnotes shows per page; Orientation/Auto Turn are popups; QR encodes page text; bookmark creation needs the Long-press Menu setting or the menu row; bookmark deletion opens a Cancel/Delete popup with Cancel preselected; footnotes are reached via menu/power/touch, not in-page selection.
- §7: the serial screenshot is host-initiated (`CMD:SCREENSHOT`); the recovery picker is a second boot-time hold.

### README.md

- Formats list omits `.fb2` (fork) and `.md`; describes papyrix as the FB2 fork; "USB Drive mode (X4Pro)" also covers X4 Classic and Paper Mono; tilt "X3 and Sticky" vs USER_GUIDE "X3 only" (code: any board with an IMU).

### docs/

- `file-formats.md`: ImHex pattern says `EXPECTED_VERSION 41` (code 45); the page pattern lacks the v44 link records and the TextBlock ruby strings; `RUBY_CONTINUE=64` missing from WordStyle; `css_rules.cache`, `html/`, `.part`, `img_*`/`.pxc`, covers, and all TXT/XTC/FB2 caches and JSON stores are undocumented.
- `webserver.md`/`webserver-endpoints.md`: uploads do **not** overwrite; `/api/status` also returns `serial` and non-X3/X4 device names; the WebSocket error table lacks `File already exists`; zero-size START skips READY; dotfiles stay inaccessible to download/rename/move/delete regardless of the setting.
- `sd-card-fonts.md`: fonts are selected in Text Settings > Font/Size tabs (no "Font Family" menu); the browser's group screen, Download All/Update All and delete flow are undocumented; registry accepts any `<name>_<size>.cpfont` basename.
- `dictionary.md`: case folding is ASCII-only; an unusable `.sidx` reports misses as "Couldn't read definition"; the 64 KB definition cap, 255-byte headword limit and English-only stemmer are undocumented.
- `activity-manager.md`: the render task is pinned to core 1 on multi-core parts; `std::function` result handlers still exist.
- `contributing/architecture.md`: mentions the removed `ActivityWithSubactivity.h`; describes the C3/X4 only; omits FB2; persisted-areas list is incomplete.
- `contributing/development-workflow.md`, `testing-debugging.md`, `getting-started.md`: omit the host test program, `bin/run-tests`, the pre-push hook; say "branch from develop" (fork: `master`); hooks doc omits pre-push.
- `i18n.md`: 32 languages (34); key-format rules are not enforced by the generator; missing keys are silent unless verbose.
- `test/corpus/README.md`: the html corpus suite links the system expat (`XML_GE=1`).
- `ROADMAP.md`: "Transparent sleep screens shelved" contradicts the implemented Transparent mode.
- `docs/comparison.md`: stale (0.5.1).

### AGENTS.md / CLAUDE.md

- `gh_release` is `LOG_LEVEL=1`, not 0; environment list omits the S3 envs; `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` lives in `lib/miniz/src/MinizConfig.h`, not `platformio.ini`; HTML sources are in `src/network/html/`, not `data/html/`; the HAL table omits HalClock/HalFrontlight/HalPowerManager/HalSystem/HalTiltSensor; `HalFile::size()/isOpen()` do not take the mutex; `GfxRenderer.cpp:439-440` is not the bitmap malloc site; cache versions are 10/45, not 7/25; `storeBwBuffer` is only the grayscale fallback; the CI table mislabels `pr-formatting-check.yml` (it checks PR titles).

### Source comments

- `HttpDownloader.h` claims CA verification (the shipped wolfSSL path calls `setInsecure()`); `HalDisplay.h` states "Half refresh (1720ms)" without a measurement; `FirmwareFlasher.h`/`OtaBootSwitch.h`/`SdFirmwareUpdateActivity.h` describe an OTA-through-SD path and the Arduino Update API that are not used; `KOReaderSyncClient.h` names the legacy default server; `NetworkModeSelectionActivity.h` lists three modes; `OtaBootSwitch.h` references a non-existent `patch_firmware_image.py`; `STR_RECOVERY_MODE_HINT` names `firmware.bin` although any `.bin` is accepted; `freeink-sdk/docs/consumer-mcu-portability.md` says the HAL fails on S3.

## 4. Known limitations register

Behaviour that is bounded by design or not yet implemented; candidates for future features rather than defects.

- Renaming or moving a book by hand orphans its caches and progress (caches are keyed by a hash of the path).
- CSS: tag/class/tag.class selectors only; no font-size, font-family, colour, borders, floats, list styles; lists always bulleted; tables ≤4 columns; images always block-level; only JPEG/PNG images and covers; progressive JPEG at DC-only quality; 8 MP cap.
- Hyphenation: 10 languages; one global language per session; words >68 codepoints skipped.
- Reader: footnote return stack of 3; QR export ≤2953 bytes; end-of-book suggestions ≤3, same folder, later in sort order; "Quick-return from footnotes" has no effect; toolbar menu only on touch boards; percent dialog EPUB/FB2 only.
- Formats: TXT assumes UTF-8, no Markdown rendering, no justification, no reader menu; XTC pages are blitted 1:1 in portrait, status overlay on 1-bit pages only; FB2 keeps only the first author, no footnotes/bookmarks/dictionary, `.fb2.zip` not recognised, cp1251/cp1252 only.
- Fonts: one SD reader size resident; CJK UI fallback is per string and needs 8/10/12 pt files; family names ≤31 bytes selectable; bidi lines ≤128 codepoints; update detection by file size; font binaries over plain HTTP with CRC32.
- Library: no on-device rename/move/multi-select/mkdir; fixed sort; recents ≤10; option popups ≤16; cover generation blocks the UI.
- Network: no authentication or TLS on the server; open hotspot; single WebSocket upload; uploads never overwrite (WebDAV PUT does); files-only rename/move; empty-folder delete; WebDAV Class 1 with fixed dates; ≤8 Wi-Fi networks; XOR obfuscation is not encryption.
- OPDS: EPUB acquisition links only; 62 entries per page; Basic auth only; blocking download.
- Updates: only `firmware.bin`/`firmware-<board>.bin` recognised (upstream renamed assets); X4 Classic has no release asset; no OTA rollback; any `.bin` accepted from SD.
- KOReader: uploads quantised to paragraph/page fraction; percentage-based ordering can contradict content order; `li[N]` xpointers fall back to percentage; TLS unverified; plain password in Basic header; synchronous requests; every session ends in a silent reboot.
- Dictionary: 32-bit index offsets; uncompressed `.idx`; 64 KB definitions; ASCII case folding; English stemming; one dictionary; EPUB reader only.
- Platform: no ZIP64 or CRC checks; 256 KB in-RAM ZIP reads; PNG no Adam7; 16 log lines survive a warm reboot only; single crash report; USB Drive on SDMMC boards only; `x4c` not in CI/release; SPIFFS reserved unused.
- Tests: two suites on system expat; no simulator environment in-repo; `--quick` omits two suites; bash-only tooling. (The ASan CI job landed in Phase 20; `ci.yml`'s `unit-tests` matrix runs plain and `-DCROSSPOINT_SANITIZE=ON`.)

## 5. Test program summary

59 suites (see `plan.md` Technical Context): alloc_guards, book_metadata_cache, chapter_html_slim_parser, chapter_xpath_resolver, combining_marks, content_opf_parser, credential_integrity, css_parser, dict_html_pages, dict_zip, dictionary, differential_rounding, direct_pixel_writer, epub_orchestration, epub_section, fb2_book, fb2_cover_extractor, fb2_filename, fb2_metadata_parser, fb2_section_cache, fb2_section_parser, firmware_flash, font_cache_manager, font_decompressor, font_system, gfx_renderer, html_to_plain_text, hyphenation_eval, image_dims_probe, jpeg_to_bmp, kosync_client, library_helpers, ligature_guard, minibidi_arabic, network_helpers, opds_filename, opds_parser, ota_asset_selection, page_link, persistable_stores, platform_helpers, png_decode, progress_mapper, reader_helpers, release_json_parser, sdcard_font, serialization, settings_board_variants, settings_input, streaming_json_parser, text_block_layout, token_boundary, txt_reader, utf8_compose, web_dav_paths, xml_parser_utils, xtc_fb2_readers, xtc_parser, zip_file. Instruments: `AllocCounter` (operator new shim), `AllocGuard` (malloc interposer with simulated OOM), an 87-file malformed-input corpus with universal-invariant and pinned-outcome tests, hyphenation accuracy thresholds against pyphen ground truth.
