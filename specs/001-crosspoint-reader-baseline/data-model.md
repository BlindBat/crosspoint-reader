# Data Model: CrossPoint Reader Firmware (Retrospective Baseline)

Entities are grouped by the subsystem that owns them. "Persisted" names the on-card or in-flash location; binary layouts are in `contracts/cache-formats.md` and the other contract files.

## 1. Books and caches

| Entity | Attributes | Persisted |
|--------|------------|-----------|
| **Epub** | path, cache path (`/.crosspoint/epub_<std::hash(path)>`), content base path, NCX/nav item hrefs, CSS file list, metadata cache, CSS parser | directory per book |
| **BookMetadata** | title (NFC), author (all creators joined by ", "), language, coverItemHref, textReferenceHref | `book.bin` v10 |
| **SpineEntry** | href, cumulativeSize (u32, uncompressed bytes through this item), tocIndex (s16; −1 none; inherited when no direct entry) | `book.bin` |
| **TocEntry** | title, href, anchor, level (u8, 1-based), spineIndex (s16; −1 unresolved) | `book.bin` |
| **CssRuleStore** | sorted selector index, selector pool (≤32 KB), interned styles (≤256), ≤1500 rules, partial flag | `css_rules.cache` v12 |
| **CssStyle** | textAlign, fontStyle, fontWeight, textDecoration, direction, textIndent, margins, paddings, imageWidth/Height, display, verticalAlign, defined-bits | in rule store |
| **ReaderRenderSpec** | fontId, lineCompression, extraParagraphSpacing, paragraphAlignment (0..4), viewportWidth/Height, hyphenationEnabled, embeddedStyle, imageRendering (0..2), focusReadingEnabled | section header (cache key) |
| **Section** | spineIndex, page count, current page, build context (parser, LUT of page starts, byte progress, EMA estimate), partial/complete flags, watermark | `sections/<n>.bin` v45, `.part`, `html/<n>.html` |
| **Page** | elements (PageLine/PageImage/PageHorizontalRule), footnotes (≤16), links (≤32), visibleTextOffset | page record |
| **TextBlock** | word count, text bytes, per-word offsets/x positions/styles, optional focus suffix-x and boundary, ruby strings, block style | page record |
| **ImageBlock** | imagePath (`img_<spine>_<n>.<ext>`), srcPath, width, height; session failure list (≤16); pixel-cache RAM slot | page record; `.pxc` |
| **BlockStyle** | alignment, margins/paddings (px), textIndent, isRtl, direction/indent/align defined flags | per TextBlock |
| **FootnoteEntry / PageLink** | number[32], href[256]; PageLink adds x, y, width, height (s16) | page record |
| **Progress record (EPUB)** | spineIndex u16, pageNumber u16 (0xFFFF = stale), pageCount u16, visibleTextOffset u32 (optional) | `progress.bin` (4/6/10 bytes) |
| **BookmarkEntry** | xpath, percentage (0..1), summary (≤72 chars), computed spine/page-count/progress, visibleTextOffset (optional) | `/.crosspoint/bookmarks/<flattened path>.json` |
| **Txt** | path, cache path (`txt_<hash>`), file size, title (name minus `.txt`) | `index.bin` v3 (page offsets + settings signature), `progress.bin` (u16 page), `cover.bmp` |
| **XtcHeader / PageInfo / ChapterInfo** | magic, version, pageCount, flags, offsets; per-page offset/size/width/height/bitDepth; chapter name (≤80), start/end pages | read from `.xtc/.xtch`; caches `xtc_<hash>/progress.bin` (u32 page), `cover.bmp`, `thumb_<h>.bmp` |
| **Fb2 (fork-only)** | title, author, language, coverBinaryId, sections[{title, fileOffset, length}], tocEntries[{title, sectionIndex}] | `fb2_<hash>/book.bin` v2, `sections/<n>.bin` v4, `progress.bin` (section, page, count), `cover.bmp`, `thumb_<h>.bmp` |

## 2. Runtime state and preferences

| Entity | Attributes | Persisted |
|--------|------------|-----------|
| **CrossPointSettings** | ~60 fields: sleepScreen, sleepScreenCoverMode/Filter, quickResumeSleepScreen, hideBatteryPercentage, refreshFrequency, uiTheme, fadingFix, frontlightRestoreOnWake, screenInverted, fontFamily, fontPointSize, sdFontFamilyName[32], lineSpacing, screenMargin, paragraphAlignment, embeddedStyle, focusReadingEnabled, hyphenationEnabled, orientation, extraParagraphSpacing, textAntiAliasing, imageRendering, readerMenuStyle, sideButtonLayout, touchReaderControls, showReaderMenu, frontButtonFollowOrientation, frontButtonBack/Confirm/Left/Right, longPressButtonBehavior, longPressMenuFunction, shortPwrBtn, tiltPageTurn, pwrBtnFootnoteBack, backShortToFileBrowser, sleepTimeoutMinutes, showHiddenFiles, removeReadBooksFromRecents, moveFinishedToReadFolder, opdsDownloadFolder[64], opdsFilenameFormat, frontlightBrightness/Warmth/On, statusBar* (chapter count, percent, progress bar, thickness, title, battery, clock, format, UTC offset, synced), xtcStatusBarMode, dictionaryName[32], language, keyboardLayouts | `/.crosspoint/settings.json` (flat, unversioned, migrated by shape) |
| **StatusBarSpec** | snapshot: showChapterPageCount, showBookProgressPercent, titleMode, showBattery, showBatteryPercent, clockMode, clock12h, clockUtcOffsetQ, progressBarMode, progressBarHeightPx, xtcMode | derived |
| **CrossPointState** | openEpubPath, recentSleepImages[16]+pos/fill, recentOverlaySleepImages[16]+pos/fill, readerActivityLoadCount, lastSleepFromReader, showBootScreen | `/.crosspoint/state.json` |
| **RecentBook / RecentBooksStore** | path, title, author, coverBmpPath (with `[HEIGHT]` placeholder); ≤10, most recent first | `/.crosspoint/recent.json` |
| **OpdsServer / OpdsServerStore** | name, url, username, password; ≤8 | `/.crosspoint/opds.json` (password_obf) |
| **WifiCredential / WifiCredentialStore** | ssid, password (≤64); lastConnectedSsid; ≤8 | `/.crosspoint/wifi.json` (password_obf, password_len, password_crc32) |
| **KOReaderCredentialStore** | cfgVersion (2), username, password, serverUrl, matchMethod (Filename/Binary), sendMetadata, syncBehavior (Ask/Smart) | `/.crosspoint/koreader.json` (password_obf) |
| **SilentReboot flag / PanicCapture / LogRing** | magic 0xC1EAB007 + target (0 home, 1 reader); panicMessage[256], panicStack[32]×{sp, 8 words}, marker 0x50414E49; logMessages[16][256], head, magic 0xDEADBEEF | RTC_NOINIT memory (warm reboot only) |
| **SleepFrame** | raw framebuffer bytes (= display buffer size) | `/.crosspoint/sleep_frame.bin` |

## 3. Fonts and rendering

| Entity | Attributes | Persisted |
|--------|------------|-----------|
| **Framebuffer** | 800×480 1-bpp panel buffer (48,000 bytes), strip target, BW snapshot chunks (8,000 bytes each) | RAM (SDK-owned) |
| **Orientation / RenderMode / RefreshMode** | Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise; BW, GRAYSCALE_LSB, GRAYSCALE_MSB; FULL, HALF, FAST | `orientation` setting |
| **EpdFontData / EpdGlyph / EpdFontFamily** | bitmaps, glyph records (w, h, advanceX 12.4, left, top, dataLength, dataOffset), intervals, metrics, compression groups, kern classes (split+sparse for flash, packed+dense for SD), ligature pairs, miss/coverage callbacks; family = regular/bold/italic/bold-italic | flash (built-in) or reconstructed from `.cpfont` |
| **SdCardFont** | per-style headers, section offsets, interval tables (compact 6-byte form when possible), kern classes, ligatures, stub/mini `EpdFontData`, mini kern matrix, 8-slot overflow ring, ≤768-entry advance table per style, content hash | reads `.cpfont` v4 |
| **SdCardFontFamilyInfo / SdCardFontFileInfo** | directory name, files (path, pointSize), available sizes | derived from `/.fonts`, `/fonts` |
| **ManifestFamily / ManifestFile** | name, description, styles, files (name, size, crc32), totalSize, installed, hasUpdate, scriptMask | `fonts.json` (remote) |
| **ThemeMetrics / BaseTheme** | per-theme geometry and style table; drawing interface (header, menu, cover tile, hints, popup, status bar, text field) | flash `constexpr` |

## 4. Network and services

| Entity | Attributes | Persisted |
|--------|------------|-----------|
| **NetworkMode** | JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, USB_DRIVE | — |
| **WifiNetworkInfo** | ssid, rssi, isEncrypted, hasSavedPassword, isHiddenPlaceholder | — |
| **UploadState / WsUploadStatus** | file, name, path, size, 4 KB buffer; inProgress, received, total, filename, last completed | — |
| **UsbDriveState** | Unsupported, WaitingForHost, Connected, Ejected, Disconnected, IoError | — |
| **OpdsEntry** | type (NAVIGATION/BOOK), title (≤160), author (≤120), href (≤768), id (≤128) | — |
| **OpdsParser** | entries (≤62), searchTemplate, next/prev page URLs, error, truncated flags | — |
| **KOReaderProgress** | document, progress (xpointer), percentage, device, deviceId, timestamp, optional metadata {filename, title, authors}, optional position {pctQ, spine, page, pages, para, xpath} | wire JSON |
| **CrossPointPosition / SavedProgressPosition** | spineIndex, pageNumber, totalPages, visibleTextOffset, paragraphIndex, liIndex, xpathAnchorId; xpath + percentage | — |
| **ReleaseJson result** | tagName[32], firmwareUrl[512], firmwareSize, assetName | — |
| **Firmware image / board tag / otadata SelectEntry** | ESP image header (magic 0xE9, segments, chip_id, hash_appended); `CROSSPOINT-BOARD-V1:<board>;`; ota_seq, label, state, crc | flash partitions |
| **Dictionary / LookupSession / Sidecar** | basePath, hasPlainDict, hasSyn, htmlDefinitions; open `.idx`/`.qidx`/`.syn`/`.sidx` handles; sidecar header {magic, version 2, interval 256, sampleCount, sourceFileSize, entryCount} + offsets | `<stem>.qidx`, `<stem>.sidx` next to the dictionary |
| **DictZip::Info** | dataOffset, totalSize, chunkLength, chunkOffsets (≤8193) | — |

## 5. UI framework

| Entity | Attributes | Persisted |
|--------|------------|-----------|
| **Activity** | name, lifecycle (onEnter/loop/render/onExit), predicates (skipLoopDelay, preventAutoSleep, requiresExclusiveStorageLoop, isReaderActivity, handleForcedRefresh, isHomeActivity, handleHomeGesture, getScreenshotInfo), result handler | — |
| **ActivityManager** | current activity, stack (reserve 10), pending action (None/Push/Pop/Replace), render task, render mutex, deferred update flag | — |
| **ActivityResult** | isCancelled; variant of WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult, IntervalResult, PageResult, ProgressChangeResult, NetworkModeResult, FootnoteResult, FilePathResult | — |
| **MappedInputManager::Button** | Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward, NavNext, NavPrevious, ScreenLeft/Right/Up/Down | — |
| **ScreenshotInfo** | readerType (None/Epub/Txt/Xtc/Fb2), title[64], spineIndex, currentPage, totalPages, progressPercent | encoded in file name |

## 6. State machines

- **Boot**: reset reason + USB + RTC flags → `BootResume::{Splash, Silent, SplashlessWake}`; then SD mount → crash check → recovery check → resume-book check → Home / Reader / Crash / Recovery / "SD card error".
- **Sleep**: idle timer or Power hold → record origin → save state → render SleepActivity (mode-specific) → optional `sleep_frame.bin` → tear down Wi-Fi/tilt/panel/SD → deep sleep; wake → verify hold (10 ms) → splashless resume.
- **Activity stack**: replace (destroy current + stack) / push (`startActivityForResult`) / pop (`finish` → parent handler → re-render); empty stack → Home.
- **Wi-Fi selection**: AUTO_CONNECTING → SCANNING → NETWORK_LIST → HIDDEN_SSID_ENTRY / PASSWORD_ENTRY → CONNECTING → CONNECTED / CONNECTION_FAILED → SAVE_PROMPT / FORGET_PROMPT.
- **Web server activity**: MODE_SELECTION → WIFI_SELECTION | AP_STARTING → SERVER_RUNNING → SHUTTING_DOWN (silent restart).
- **USB Drive**: Preparing → WaitingForHost (5 min) → Connected → Ejected/Disconnected/IoError → restart to Home.
- **OPDS browser**: CHECK_WIFI → WIFI_SELECTION → LOADING → BROWSING ↔ SEARCH_INPUT → DOWNLOADING → (reload) | ERROR (retry).
- **KOReader sync**: WIFI_SELECTION → CONNECTING → SYNCING → SHOWING_RESULT / NO_REMOTE_PROGRESS → UPLOADING → UPLOAD_COMPLETE | SYNC_COMPLETE | SYNC_FAILED | NO_CREDENTIALS (auto-return 1.2 s).
- **SD firmware update**: PICKING → VALIDATING → CONFIRMING → UPDATING → SUCCESS (restart) | FAILED (recovery mode returns to PICKING).
- **Dictionary lookup**: word select (highlight, differential repaint) → open dictionary once per session → build stale sidecars → lookup (exact → syn → stems) → definition screen (HTML pages or plain text) → back to word select.
