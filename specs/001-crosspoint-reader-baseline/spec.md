# Feature Specification: CrossPoint Reader Firmware (Retrospective Baseline)

**Feature Branch**: `001-crosspoint-reader-baseline`

**Created**: 2026-09-15

**Status**: Baseline (retrospective, verified) — describes the firmware as it ships on fork `master` (upstream 1.6.0 lineage, commit `a54eabf9`, 2026-09-14); every requirement and scenario was checked against the code on 2026-09-15 (284 items: 191 confirmed as written, 93 corrected)

**Input**: User description: "There is no any spec yet in this project. I want to have the specs and implementation plan in speckit's format, retrospectively. So, I want you to deeply analyse the original product and the code, and deduce the full spec"

## Scope Note

This specification was reverse-engineered from the shipped product: the code under `src/`, `lib/`, `test/`, the build configuration, and the user-facing documentation (README, USER_GUIDE, docs/). Where the documentation and the code disagree, **the code is treated as the source of truth** for this baseline; every such disagreement is catalogued in the plan's research notes (`research.md`, "Documentation drift register") so it can be resolved deliberately rather than silently.

The firmware is a dedicated e-book reader for ESP32-based e-ink devices (Xteink X4/X3 on ESP32-C3; Xteink X4 Pro, X4 Classic, Seeed reTerminal Sticky and M5 Paper Mono on ESP32-S3). Its mission, per `SCOPE.md`, is to do one thing well: focused reading. Capabilities that exist only on this fork (FictionBook 2 support, the host test program, several input-hardening fixes) are marked *(fork-only)*.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read an EPUB and keep my place (Priority: P1)

A reader picks an EPUB on the SD card, the book opens at the position they last left, pages turn with a single button press or tap, and the position survives sleep, reboot, and any later change of font, margin or orientation.

**Why this priority**: This is the product. Every other capability exists to make this journey better.

**Independent Test**: Copy one EPUB to the SD card, open it from the file browser, turn ten pages, hold Power to sleep, wake, and confirm the same page is shown; then change the font size and confirm the same text is still on screen.

**Acceptance Scenarios**:

1. **Given** an EPUB that has never been opened, **When** the reader selects it, **Then** an "Indexing" popup is shown while the book index is built (book.bin absent), the first content page is displayed (the spine item named by an OPF guide reference of type `start` when one exists; guide `text` references are ignored and the book otherwise opens at spine index 0), and the book is added to Recent Books once the load succeeds.
2. **Given** an open book, **When** the reader presses the next-page control (front Right in the unswapped orientations, side Down with the default Prev/Next side layout, a right-third tap in Tap mode or a left swipe in Swipe mode of Touch Reader Controls, a forward tilt when Tilt Page Turn is on, or a short Power click when Short Power Button Click is Page Turn), **Then** the next page is displayed with a fast partial refresh (FAST_REFRESH), and a clean HALF_REFRESH is used instead every N pages per the Refresh Frequency setting (1/5/10/15/30, default 15).
3. **Given** the last page of a chapter, **When** the reader turns forward, **Then** the first page of the next spine item is displayed; turning back from the first page of a chapter shows the last page of the previous one.
4. **Given** a book with a saved position, **When** the device reboots or wakes after sleeping from the reader, **Then** the book reopens on the same page without user action.
5. **Given** a saved position, **When** the reader changes font family, size, line spacing, margin, alignment, hyphenation, embedded style, image mode, focus reading or orientation, **Then** the chapter is re-paginated and the page containing the same text is shown.
6. **Given** a book whose book.bin or section cache files are corrupt or truncated, **When** it is opened, **Then** the cache is rejected (version/header/extent checks) and rebuilt without crashing, and the reader lands on the chapter recorded in progress.bin; a page that still fails to load clears that section cache and is retried up to three times before "Page load error" is shown. A progress.bin of unexpected size is ignored, in which case the book opens at its start page.

---

### User Story 2 - Find and open books on the SD card (Priority: P1)

From the Home screen the reader continues the most recent book, browses folders on the card, opens recently read books, or views a BMP/PNG image.

**Why this priority**: Without library navigation the reader cannot reach a book; this is the second half of the minimum product.

**Independent Test**: Place books in nested folders; from Home open Browse Files, descend into a folder, open a book, return Home and confirm it appears as the Continue Reading tile and in Recent Books.

**Acceptance Scenarios**:

1. **Given** the Home screen, **When** it is displayed, **Then** it shows the Continue Reading tile (one book in Classic, Lyra and RoundedRaff; three in Lyra Extended) and the menu Browse Files, Recent Books, [OPDS Browser when at least one server is configured], File Transfer, Settings; in RoundedRaff a "Continue Reading" row is additionally inserted at the top of the menu (and the header shows the most recent book's title) whenever a recent book exists.
2. **Given** the file browser, **When** a folder is listed, **Then** directories come first, entries are in natural (numeric-aware, case-insensitive) order, only supported files (.epub, .fb2, .xtc/.xtch, .txt, .md, .bmp, .png) are shown, and dot-prefixed entries are hidden unless Show Hidden Files is on.
3. **Given** a listed book, **When** Confirm is released (or the row is tapped), **Then** the book opens in the reader matching its extension; a .bmp/.png opens in the image viewer.
4. **Given** a listed entry, **When** Confirm is held for one second (or a row is long-pressed on touch), **Then** a delete confirmation is shown and, on confirm, the entry (recursively for folders) and its book caches are removed.
5. **Given** Recent Books, **When** the screen opens, **Then** up to ten books are listed most-recent-first with title and author, entries whose files are gone are pruned, and holding Confirm removes an entry after confirmation.
6. **Given** Home, **When** Back is released, **Then** the most recent existing book opens.

---

### User Story 3 - Turn on, sleep, wake and recover safely (Priority: P1)

The device boots to the last book or Home, sleeps after inactivity or a Power hold showing the chosen sleep screen, wakes without a splash, and never boot-loops on a crashing book.

**Why this priority**: Battery life and stability on a 380 KB device are non-negotiable; a device that cannot sleep or recover is unusable.

**Independent Test**: Set Time to Sleep to 1 minute, leave the device idle, confirm the selected sleep screen appears, press Power, confirm the previous screen returns without the boot splash.

**Acceptance Scenarios**:

1. **Given** a cold boot, **When** the device starts, **Then** the boot splash (logo, name, version) is shown, then Home, or the last book when the last sleep was initiated from the reader.
2. **Given** an idle device, **When** Time to Sleep minutes elapse with no button, touch or tilt activity and no activity forbids sleep, **Then** the device renders the sleep screen and enters deep sleep.
3. **Given** the device is asleep, **When** Power is pressed and still held after the 10 ms stable-sample verification window, **Then** the device wakes without the splash and paints Home, or the last book when the last sleep was initiated from the reader (a Quick Resume frame, if present, is shown with a loading icon first); a ghost wake that fails verification returns to deep sleep unless Short Power Button Click is set to Sleep.
4. **Given** the "Quick Resume" sleep screen, **When** the device sleeps and wakes, **Then** the last page stays on screen with a moon icon during sleep and the book resumes directly on that page.
5. **Given** a book that crashed while loading on the previous boot, **When** the device boots, **Then** it does not retry the book (the crash-loop counter is non-zero); if the reset was a panic, CPU lockup or a watchdog reset carrying the panic marker, a crash report has been written to `/crash_report.txt` on the SD card root and a "System Crash" screen is shown first, going to Home when dismissed; otherwise it goes to Home directly with no report.
6. **Given** Back held while the device wakes from deep sleep by the Power button, **When** the device starts, **Then** Home is shown regardless of the last-book state. On a cold boot (power-on/after-flash reset) the buttons are not sampled before this check, so a held Back does not affect routing; silent restarts, panic reboots and recovery mode bypass the check as well.

---

### User Story 4 - Navigate inside a book (Priority: P2)

The reader jumps between chapters, follows footnotes and links and returns, jumps to a percentage, and drops, opens and deletes bookmarks.

**Why this priority**: Long-form reading needs navigation, but a book can be read linearly without it.

**Independent Test**: Open an EPUB with a table of contents and footnotes; open the reader menu, select a chapter, open a footnote, return, drop a bookmark, reopen it from the bookmark list.

**Acceptance Scenarios**:

1. **Given** the reader with the list-style reader menu (always on button boards; on touch boards only when Reader Menu Style is List, since they default to the Toolbar overlay), **When** Confirm is released (or the configured touch menu gesture is used, or the Home key is held with Long-press Menu set to Reader Menu), **Then** the reader menu opens with, in order: Select Chapter, Footnotes (only when the current page has footnotes), Bookmarks (only when any exist), Toggle Bookmark, Text Settings, Night Mode, Frontlight (only when present), Look Up, Reading Orientation, Auto Turn (Pages Per Minute), Go to %, Take screenshot, Show page as QR, Go Home, Sync Progress, Delete Book Cache. In Toolbar style the More panel lists the same rows without Select Chapter and Text Settings.
2. **Given** Select Chapter, **When** a chapter is chosen, **Then** the reader jumps to that spine item and anchor; Back returns to the menu.
3. **Given** a page with footnote links, **When** a footnote is chosen from the Footnotes list (or a link box is tapped on touch hardware with Touch Reader Controls enabled, or Power is clicked in Footnotes mode: a single footnote is followed directly, several open the Footnotes list, and a click while inside a footnote returns), **Then** the target is shown and a short Back (under 1 s) returns to the origin; up to three return positions are stacked, further jumps are made without saving one.
4. **Given** the Go to % dialog, **When** a percentage is confirmed, **Then** the reader lands on the proportional page of the spine item covering that fraction of the book's bytes.
5. **Given** a page, **When** the bookmark toggle is triggered (menu row, the toolbar More-panel row, or a 400 ms Confirm hold when Long-press Menu is set to Bookmark), **Then** a bookmark with a text summary is stored; "Bookmark added." is shown for the hold and More-panel paths (the list-menu row toggles without a popup); triggering it again on the same page removes it ("Bookmark removed." on those same paths).
6. **Given** the Bookmarks list, **When** an entry is opened, **Then** the reader returns to that text even after a re-pagination; holding Confirm on an entry offers Cancel/Delete.
7. **Given** the last page of the book, **When** the reader turns forward, **Then** an end-of-book screen lists up to three books ordering after the current one in the same folder plus a Home row; when no such book exists the screen shows only the "End of book" title, a further forward turn goes Home and a back turn returns to the last page.

---

### User Story 5 - Tune typography, layout and display (Priority: P2)

The reader chooses font family and size, spacing, margins, alignment, hyphenation, embedded styles, image mode, focus reading, anti-aliasing, orientation, status bar composition, refresh cadence, night mode and UI theme, and sees a live preview of text settings.

**Why this priority**: Legibility is the core value of an e-reader; every setting here directly serves reading, but the defaults already read well.

**Independent Test**: Open Settings > Reader > Text Settings, change size and alignment and watch the preview update; return to a book and confirm the new layout.

**Acceptance Scenarios**:

1. **Given** Text Settings, **When** the Font, Size, Layout or Style tab is edited, **Then** the change is saved immediately, the preview re-lays out through the real layout engine, and the reader re-paginates at the same text on return.
2. **Given** Reading Orientation, **When** Portrait, Landscape CW, Portrait 180° or Landscape CCW is chosen (from Settings, the reader menu, the control center tile, or a long page-button press when so configured), **Then** the renderer rotates and the section cache is rebuilt for the new viewport.
3. **Given** Night Mode on, **When** any screen renders, **Then** output polarity is inverted while embedded images keep their original polarity and the sleep screen stays normal.
4. **Given** Customise Status Bar, **When** chapter page count, book percentage, progress bar mode/thickness, title mode, battery, XTC bar, and (on RTC boards) clock rows are edited, **Then** the live preview and the reader status bar reflect the composition.
5. **Given** UI Theme, **When** Classic, Lyra, Lyra Extended or RoundedRaff is selected, **Then** the theme applies immediately to headers, lists, menus and popups.

---

### User Story 6 - Read TXT, XTC and FB2 books (Priority: P2)

Plain-text and Markdown files, pre-rendered XTC/XTCH page containers, and FictionBook 2 files *(fork-only)* open in format-specific readers with progress, chapter lists where the format has them, and covers on the sleep screen.

**Why this priority**: Users have libraries in these formats; each reader reuses the EPUB shell so the cost is bounded.

**Independent Test**: Place one .txt, one .xtc and one .fb2 file on the card; open each, turn pages, sleep and wake, and confirm each resumes on its page.

**Acceptance Scenarios**:

1. **Given** a .txt or .md file, **When** it is opened, **Then** it is paginated by streaming with a cached page index, honours alignment and anti-aliasing, and resumes on its saved page.
2. **Given** an .xtc/.xtch container, **When** it is opened, **Then** its header is validated (XTC/XTCH magic, version 1.0 or byte-swapped 0.1, page table), pages are shown in portrait at their native pixels (1-bit pages pixel-blitted; 2-bit XTCH pages drawn as a base pass, two grayscale plane passes and a cleanup pass), chapter selection is offered when the container declares chapters, and the optional XTC status bar overlay is drawn on 1-bit pages only (the grayscale path returns before the overlay).
3. **Given** an .fb2 file, **When** it is opened, **Then** its metadata and top-level sections are indexed (including windows-1251/1252 documents), sections are laid out with the EPUB layout pipeline, and Select Chapter, Text Settings, Go to %, orientation, Go Home and Delete Cache work from the reader menu.
4. **Given** any of these formats, **When** the device sleeps with Cover mode selected, **Then** a cover derived from a sibling image (TXT), page 0 (XTC) or the embedded binary (FB2) is shown.

---

### User Story 7 - Read in any script with SD-card fonts (Priority: P2)

The reader installs `.cpfont` families on the SD card (downloaded on-device from the font catalog, uploaded from the web UI, or copied manually), selects them as the reading font, and gets CJK titles in the UI and RTL/Arabic-shaped text in books.

**Why this priority**: Built-in fonts cover Latin, Cyrillic and Vietnamese only; other scripts are unreadable without this.

**Independent Test**: Download a family from Settings > Reader > Manage Fonts, select it in Text Settings, open a book in that script, and confirm glyphs render; confirm a CJK file name renders in the browser list.

**Acceptance Scenarios**:

1. **Given** Manage Fonts with Wi-Fi available, **When** a script group and family are chosen, **Then** the family's files are downloaded, CRC-verified and magic-checked, and installed under `<root>/<Family>/` where `<root>` is the root the family is already installed in, else `/.fonts` unless only `/fonts` exists on the card; the family then appears in the Font tab.
2. **Given** a family in `/.fonts/` or `/fonts/`, **When** it is selected, **Then** one point size nearest the stored size is loaded, the Size tab lists the sizes the family ships, and the section cache is keyed on the font so pages re-layout.
3. **Given** a family covering CJK selected as the reader font, **When** a UI string contains a CJK codepoint the built-in UI font lacks, **Then** the whole string is drawn with the size-matched SD font (8/10/12 pt) when the family ships those sizes.
4. **Given** Hebrew or Arabic text, **When** it is laid out and drawn, **Then** runs are reordered per UAX#9 and Arabic letters are shaped with presentation forms; combining marks are rendered when the font provides them.

---

### User Story 8 - Transfer files wirelessly or over USB (Priority: P2)

From File Transfer the reader joins a Wi-Fi network or creates a hotspot and manages the SD card from a browser (file manager, WebDAV, settings, fonts), receives books from Calibre, or, on USB-OTG boards, exposes the card as a USB drive.

**Why this priority**: The only ways to get books onto the device without removing the card.

**Independent Test**: File Transfer > Join a Network, open the printed URL in a browser, upload an EPUB, confirm it appears in Browse Files and opens.

**Acceptance Scenarios**:

1. **Given** File Transfer, **When** Join a Network is chosen, **Then** the device auto-connects to the last saved network, then other saved networks by signal, else shows a scan list with hidden-network entry and password prompt, and then shows SSID, a QR of the URL, the IP URL and `http://crosspoint.local/`.
2. **Given** Create Hotspot, **When** chosen, **Then** an open access point "CrossPoint-Reader" with captive DNS starts and the screen shows a join-QR and a URL-QR.
3. **Given** the web File Manager, **When** a file is uploaded, **Then** it streams over WebSocket (HTTP multipart as fallback), an existing name is not overwritten (the browser auto-suffixes " (2)"), and any book cache for the path is cleared.
4. **Given** a WebDAV client, **When** it mounts `http://<ip>/`, **Then** listing, download, upload, delete, mkdir, move and copy work; dot-prefixed and protected names are refused.
5. **Given** Calibre Wireless with the CrossPoint plugin, **When** "Send to device" runs, **Then** the plugin discovers the device by UDP, uploads over WebSocket, and the device shows "Receiving:" progress and "Received:".
6. **Given** a USB-OTG board (X4 Pro, X4 Classic, Paper Mono), **When** USB Drive is chosen and a host connects, **Then** the SD card appears as mass storage and the device reboots to Home on eject.
7. **Given** any Wi-Fi mode, **When** the activity exits, **Then** Wi-Fi is disconnected and the device restarts silently to Home (touch boards shut Wi-Fi down in place).

---

### User Story 9 - Browse and download from OPDS catalogs (Priority: P2)

The reader saves up to eight OPDS servers, browses a catalog's folders, searches, pages through results, and downloads an EPUB to a chosen folder.

**Why this priority**: The only on-device way to acquire new books; bounded to the existing OPDS connector per SCOPE.md.

**Independent Test**: Add a Calibre content server URL ending in `/opds`, open OPDS Browser from Home, navigate to a book, download it, and open it from Browse Files.

**Acceptance Scenarios**:

1. **Given** Settings > System > OPDS Servers, **When** Add Server is used, **Then** name, URL, username and password are entered and saved field by field, and the Home menu gains OPDS Browser.
2. **Given** the browser, **When** a feed loads, **Then** navigation entries and books (with author) are listed, "« Previous Page"/"Next Page »" rows appear when the feed links them, and a search icon appears when the feed offers an OpenSearch template.
3. **Given** a book row, **When** it is confirmed, **Then** the EPUB is downloaded with progress and Cancel into the download folder as `<Author - Title>.epub` (per the filename format setting) and its cache is cleared.
4. **Given** the download folder does not exist, **When** a download starts, **Then** it is created, and if creation fails the SD root is used.

---

### User Story 10 - Configure controls, input and the frontlight (Priority: P2)

The reader remaps front buttons, swaps side buttons, chooses power-click and long-press behaviours, enables tilt page turn and touch controls, and adjusts the frontlight from a control center.

**Why this priority**: Different hardware and hands; the defaults mirror the stock firmware so this is refinement.

**Independent Test**: Remap Back and Confirm, confirm the button hints update and the new mapping works; set Short Power Button Click to Page Turn and turn a page with Power.

**Acceptance Scenarios**:

1. **Given** the remap wizard, **When** four distinct physical front buttons are pressed for Back, Confirm, Left and Right, **Then** the mapping is saved; a duplicate press is rejected; Side Up resets to defaults.
2. **Given** Short Power Button Click, **When** set to Ignore, Sleep, Page Turn, Refresh Screen, Footnotes or (touch builds) Confirm, **Then** a short Power release performs that action while a 400 ms hold (10 ms in Sleep mode) still sleeps.
3. **Given** an IMU board with Tilt Page Turn on, **When** the device is flicked forward or back while reading, **Then** a page turns, with a cooldown before the next.
4. **Given** a touch board, **When** the top edge is swiped down (frontlight boards only) or the status band (top 44 px) is tapped on the Home, Browse Files, Settings or Network screens, **Then** the control center opens; on frontlight boards it shows brightness (1..100) with a lamp toggle and warmth where the light supports colour temperature, and on touch boards quick tiles (Night Mode, Refresh, Orientation, Touch on/off).
5. **Given** X4 Pro, **When** Power is double-clicked within 500 ms, **Then** the frontlight toggles.

---

### User Story 11 - Use the device in my language (Priority: P2)

The UI is available in 34 languages, selected from Settings, with keyboard layouts for Latin, Cyrillic and Hebrew scripts.

**Why this priority**: The audience is international; all UI strings are already routed through translation tables.

**Independent Test**: Settings > System > Language > choose Deutsch; confirm menus re-render in German and the choice survives a reboot.

**Acceptance Scenarios**:

1. **Given** the language list, **When** a language is chosen, **Then** every screen re-renders in it immediately and the choice is persisted as a stable language code.
2. **Given** a string missing in a translation, **When** it is displayed, **Then** the English text is shown.
3. **Given** Keyboard Layouts, **When** layouts are enabled, **Then** the on-screen keyboard offers a language key cycling through them, and at least one Latin layout always remains.

---

### User Story 12 - Look up words in an offline dictionary (Priority: P3)

With a StarDict dictionary on the card, the reader highlights any word on the page and reads its definition, including dictzip-compressed dictionaries and HTML definitions.

**Why this priority**: A reading aid that ships in Phase 0 of the roadmap; valuable but optional.

**Independent Test**: Copy a StarDict folder under `/dictionaries/`, select it in Settings > Reader > Dictionary, open a book, choose Look Up, pick a word and confirm the definition screen.

**Acceptance Scenarios**:

1. **Given** at least one dictionary folder, **When** Settings > Reader is opened, **Then** a Dictionary row lists None plus each folder.
2. **Given** Look Up, **When** it starts, **Then** the page re-renders with a word highlighted; Left/Right step words, Up/Down change rows, Confirm looks up, Back returns; on touch a tap looks up directly.
3. **Given** the first lookup, **When** sidecar indexes are stale, **Then** "Indexing dictionary..." is shown and the sidecars are rebuilt.
4. **Given** a word, **When** looked up, **Then** exact match, dictionary synonyms (when a `.syn` exists) and English stemming are tried in that order and the definition is paged: HTML definitions (`sametypesequence=h`) up to 16 KB are laid out with the reading engine, larger or plain ones are converted to plain text and word-wrapped; a miss shows "Not found" while read, decompression and low-memory failures show distinct error popups.

---

### User Story 13 - Sync reading progress with KOReader (Priority: P3)

The reader pushes and pulls EPUB progress against a KOSync server (CrossPoint's by default) so a KOReader app or another device continues at the same text.

**Why this priority**: Multi-device readers value it; single-device readers never touch it.

**Independent Test**: Enter credentials, Authenticate, open a book, Sync Progress, confirm "Progress uploaded!"; from another client change progress, sync again and confirm the reader jumps.

**Acceptance Scenarios**:

1. **Given** username and password set, **When** Authenticate or Sign Up runs, **Then** the server's answer is shown ("Successfully authenticated!", "Account created", "Username is already registered", or the failure reason).
2. **Given** Sync Behavior "Smart sync", **When** Sync Progress runs, **Then** the device uploads when nothing is stored remotely, reports "Already synced" when positions match within 0.1 points, uploads when local is ahead, and applies remote otherwise, without asking.
3. **Given** Sync Behavior "Ask every time", **When** a remote record exists, **Then** Remote and Local chapter/page/percentage are compared and the reader chooses Apply remote or Upload local.
4. **Given** remote progress from a device with different fonts, **When** it is applied, **Then** the reader lands on the page containing the referenced text, resolved through the chapter's content offsets.

---

### User Story 14 - Update the firmware without a computer (Priority: P3)

The reader checks GitHub releases over Wi-Fi and installs the matching per-board image, or flashes a `.bin` from the SD card, including from a boot-time recovery mode.

**Why this priority**: Keeps devices current, but a USB flasher exists.

**Independent Test**: Settings > System > Check for updates on a device running an older release; confirm "New update available!", confirm Update, and confirm the device restarts on the new version.

**Acceptance Scenarios**:

1. **Given** Check for updates, **When** the latest release has a newer semantic version with an asset for this board, **Then** current and new versions are shown with Cancel/Update; when the release is not newer or carries no asset for this board, "No update available"; when the release fetch or JSON parse fails, "Update failed".
2. **Given** Update confirmed, **When** the image streams, **Then** progress is shown, images for a different chip or board are rejected, and on success the device restarts after "Update complete".
3. **Given** SD Card Firmware Update, **When** a `.bin` is picked, **Then** it is validated (magic, chip, segments, checksum, SHA-256, board tag, size), confirmed, re-validated, written with progress, and the device restarts.
4. **Given** the recovery button held while the Power button wakes or powers on the device (Up; Down on X4 Pro/X4 Classic; never on M5 Paper Mono), **When** the device boots, **Then** the firmware picker opens directly (ahead of the crash screen and resume routing) and every cancel — picker, confirmation decline or failure dismissal — returns to it.

---

### User Story 15 - Reader conveniences: auto page turn, screenshots, QR export (Priority: P3)

The reader turns pages hands-free at a chosen rate, captures screenshots, and exports the current page's text as a QR code.

**Why this priority**: Small quality-of-life additions on top of the reading loop.

**Independent Test**: Set Auto Turn to 3 pages per minute and watch pages advance; press Power+Side Down and find the BMP under `/screenshots/`.

**Acceptance Scenarios**:

1. **Given** Auto Turn set to 1, 3, 6 or 12 pages per minute, **When** reading, **Then** pages advance on that interval, the status bar shows the mode, and any Confirm/Back release stops it.
2. **Given** Power+Side Down (or the menu's Take screenshot), **When** pressed, **Then** a 1-bit BMP of the screen is saved under `/screenshots/` (in a per-book subfolder with chapter/page/percent while reading) and a border flashes.
3. **Given** Show page as QR, **When** chosen, **Then** the current page's words (truncated at a UTF-8 boundary to 2953 bytes) are shown as a QR code until Back or Confirm is released (or the screen is tapped).

---

### User Story 16 - Diagnose crashes and problems (Priority: P3)

After a crash the device writes a report to the SD card and shows a crash screen; developers get levelled serial logs, a screenshot command and periodic heap telemetry.

**Why this priority**: Field debuggability keeps the project sustainable; not a reading feature.

**Independent Test**: Trigger a panic in a debug build; on reboot confirm `/crash_report.txt` contains version, reset reason, the last log lines and a stack window, and that the crash screen appears before Home.

**Acceptance Scenarios**:

1. **Given** a panic or lockup reboot, **When** the device starts, **Then** `/crash_report.txt` is written and a "System Crash" screen with the reason precedes Home.
2. **Given** a serial monitor, **When** `CMD:SCREENSHOT` is sent, **Then** the raw framebuffer is streamed between `SCREENSHOT_START:<size>` and `SCREENSHOT_END`; heap statistics are logged every 10 s.

---

### Edge Cases

- Boot: uninitialised RTC memory on cold boot must not be mistaken for a silent-restart or crash marker (magic words guard both); a watchdog reset without a captured panic boots normally; an unmountable SD card shows "SD card error".
- Wake: a ghost/EMI wake returns to sleep unless click-to-wake is configured; a Power button still held after wake must not sleep the device until released; Power+Down must never sleep the device.
- Sleep art: a single custom image repeats; overlay BMPs whose alpha is all opaque are treated as white-transparent; BMPs larger than 2048×3072 are rejected by the header parse before any decode buffer is allocated; PNGs picked from `/.sleep-overlay/` or `/sleep-overlay/` are rejected before selection when larger than 2048×3072, over 3 MP, or interlaced, whereas the root `/sleep-overlay.png` is only bounded by the decoder's generic limits (INT16_MAX per side, 8 MP, interlace not checked) after the decoder object is allocated; a stale `sleep_frame.bin` or one of the wrong size is discarded.
- Library: a Recent Books entry whose file is gone is skipped and pruned; thumbnail generation failure clears the stored cover path; the browser opened at a non-existent path falls back to root; deletion stops at the first failure.
- EPUB structure: no parseable TOC (book continues without one), guide `text` references (ignored; only `start` is honoured), cover meta pointing at XHTML (ignored unless the manifest media-type is image/*), spine before manifest (itemrefs cannot be resolved against the not-yet-written item store and are dropped), NCX children before labels (entries are still emitted at each `content` end, in encounter order), OPDS pages with more than 62 entries (extras dropped, feed flagged truncated), stylesheets over 128 KB (skipped, CSS cache marked partial) or beyond the 1500-rule/32 KB selector/256-style caps (rule growth stops, cache marked partial) are all handled by degrading rather than failing the open; 400 or more spine items only switch the metadata cache to a hashed href index and batch size lookup.
- Caches: a section file interrupted mid-build keeps version 0 and is rebuilt; a section, book or FB2 section file truncated after validation yields a null page/rejection rather than garbage (a TXT `index.bin` truncated after its header is not extent-checked: its page count and offsets are read unchecked); any render-setting change or CSS cache change invalidates sections; renaming or moving a book orphans its caches and progress (path-hashed) unless done by the finish-to-`/read` flow.
- Layout: words longer than 200 bytes are split at UTF-8 boundaries; a word wider than the line is force-split; paragraphs over 750 words (320 with CSS) are soft-flushed; tables with more than four columns, colspan/rowspan, links or oversized cells fall back to stacked paragraphs; U+00A0/U+202F render as spaces but never break; U+2011 never breaks.
- Images: unsupported formats (GIF, SVG, WebP, BMP inside EPUBs) show alt text; progressive JPEGs decode at DC-only quality; up to 16 failed images per session are remembered and skipped; heap below the decoder gates skips the image.
- Reader: progress files of unexpected size (other than 4, 6 or 10 bytes) are ignored; page 0xFFFF is a stale sentinel; a spine index beyond the book is clamped to the end-of-book position; a page load that fails four times in a row (three retries, each clearing the section cache) shows "Page load error"; a zero-page chapter shows "Empty chapter"; the footnote stack caps at three; Go to 100% clamps to the last page.
- Bookmarks: a page already bookmarked toggles off; a bookmark with a text offset is resolved by that offset first; without one, the stored page hint is used only while it still matches the active chapter's page count, otherwise the XPath/percentage mapping is used; deleting the last bookmark closes the list.
- Settings: values written by a touch board and loaded on a button board fall back to in-memory defaults for pruned options; corrupt status-bar or clock bytes are clamped on entry; a resave rewrites the whole document (unknown keys are dropped).
- Fonts: a `.cpfont` with a wrong version, magic, style count or interval layout is refused; a family name over 31 bytes cannot be selected; the same family in both roots resolves to the hidden root; more than 128 families are truncated; a download aborted or failing CRC deletes the whole family directory.
- Network: uploads onto an existing name are refused; a second WebSocket upload is refused; a zero-size upload completes immediately; WebDAV URIs decoding to NUL are rejected; Wi-Fi loss is tolerated for five minutes before returning Home; the hotspot is open by design.
- OPDS: feeds with zero entries show "No entries found"; among an entry's `application/epub+zip` acquisition links, an href containing `.epub` or `/epub/` replaces an earlier one that has neither; pagination links inside entries are ignored; heap below the TLS floor (40 KB free or 20 KB largest block) refuses a download; a cancelled download deletes the partial file.
- Updates: a non-semver tag is never newer; an `-rc` build upgrades to the same numeric release; an untagged image is accepted, a foreign-board tag or chip id aborts; the SD card may be swapped between validation and flashing, so the image is validated again.
- KOReader: 204 and 404 both mean "no remote progress"; any 2xx is success; an xpointer index that would overflow `int` is rejected (a bad DocFragment index picks the spine by percentage; a bad step index or a tag over 11 characters disables ancestry matching and falls back to the legacy `/p[N]` paragraph-count, body-text or chapter-start modes before percentage); more than 16 steps are truncated to the first 16 rather than rejected; a NaN percentage clamps to the end of the book; uploads from page > 0 use the previous page's paragraph index.
- Dictionary: two `.idx` stems in one folder make it ambiguous and skipped; an unusable `.sidx` disables synonyms and reports misses as read errors; definitions over 64 KB are truncated; index offsets past the data file are rejected; dictzip tables over 8192 chunks are refused.
- Input: a remap that would assign one physical button to two roles is rejected; a Power click on X4 Pro in Confirm mode is delayed by the 500 ms double-click window; tilt never triggers a chapter skip.

## Requirements *(mandatory)*

Requirements are grouped by area. "System" is the firmware. Settings are named by their on-device label with the persisted key in parentheses where it helps traceability. Defaults are stated where they exist.

### Functional Requirements

#### A. Boot, power and sleep

- **FR-001**: System MUST classify every boot as Splash, Silent or Splashless-wake and MUST show the boot splash (logo, "CrossPoint", "BOOTING", version) only for Splash boots.
- **FR-002**: System MUST persist a splash-suppression flag (`showBootScreen=false` in `/.crosspoint/state.json`) before entering deep sleep, MUST honour it only when the boot is classified as a power-button wake, and on such a wake MUST re-arm it (`showBootScreen=true`, saved) before the first paint so it is one-shot; boots not classified as a power-button wake ignore the flag without re-arming it.
- **FR-003**: System MUST honour a silent-restart request only when its RTC magic word is valid, MUST coerce an out-of-range target to Home (valid targets: Home or the open book), MUST clear both before continuing, MUST NOT perform a silent restart once deep-sleep entry has begun, and on touch boards MUST shut Wi-Fi down in place instead of restarting.
- **FR-004**: After a silent restart System MUST route to Home or to the open book, MUST block until the first paint completes, and MUST absorb any buttons held during boot so they cannot open the most recent book.
- **FR-005**: On boot System MUST reopen the last book only when a book path is recorded, the last sleep was initiated from a reader, the previous automatic reopen completed (crash-loop counter is zero) and Back is not held; otherwise it MUST open Home.
- **FR-006**: Before an automatic reopen System MUST clear the recorded book path, increment the crash-loop counter and save state; the reader MUST reset the counter to zero and save state when the reader activity exits, regardless of whether the load succeeded.
- **FR-007**: System MUST show a full-screen "SD card error" message when the card cannot be mounted at boot, after initialising the display.
- **FR-008**: System MUST verify a power-button wake by requiring the button to be held at the first sample and still held after a 10 ms stable sample (debounce settled); Paper Mono, M5Paper v1.1 and boards without a power-button GPIO always pass. The sample is taken before settings load; after settings load System MUST return to deep sleep when verification failed unless Short Power Button Click is Sleep. The release that ends the wake hold MUST be consumed.
- **FR-009**: On a USB-powered cold boot (power-on reset with USB connected and no sleep wake cause) System MUST stay awake on X4 Pro, X4 Classic, Paper Mono and EEGO A4 and MUST return to sleep on every other board (X4, X3, Sticky).
- **FR-010**: System MUST enter deep sleep when Power is held longer than 400 ms (10 ms when Short Power Button Click is Sleep), not within 2 s of boot, not before the wake hold was released, and not while Side Down is also held.
- **FR-011**: System MUST enter deep sleep after Time to Sleep (`sleepTimeoutMinutes`, 1..30, default 10; 31 = Never) minutes without button, touch or tilt activity while no activity forbids auto-sleep (web server, Calibre, OPDS, OTA, font download, KOReader sync/auth, SD firmware update, USB Drive connected).
- **FR-012**: Short Power Button Click (`shortPwrBtn`, default Ignore) MUST offer Ignore, Sleep, Page Turn, Refresh Screen, Footnotes and, on touch builds, Confirm; on Paper Mono a Power release in Ignore or Sleep mode MUST power the device off.
- **FR-013**: On X4 Pro two Power releases each held ≤300 ms within 500 ms MUST toggle the frontlight, persist the state and consume the second release.
- **FR-014**: Before sleeping System MUST record whether sleep was entered from a reader, clear the splash flag, save state, render the sleep screen synchronously, save or delete the Quick Resume frame, then power down Wi-Fi, tilt sensor and display, shut the SD card down (unmount and host de-init on SDMMC boards; no-op on SPI boards), end the USB serial console, on C3 Xteink boards drive the battery latch (GPIO13) low and hold it, hold every other configured power-latch pin high, power down gated peripheral rails, on Paper Mono request a PMIC shutdown, and wait for the power button to be released before arming wake and sleeping.
- **FR-015**: System MUST lower the CPU clock (10 MHz; 80 MHz on PSRAM boards) and lengthen the loop delay after 3 s of inactivity, and MUST restore full speed on input, when an activity requests it, while Wi-Fi is active, or while a power lock is held.
- **FR-016**: System MUST enter a recovery firmware picker when a power-button boot has the recovery side button held (Up; Down on X4 Pro/X4 Classic; never on Paper Mono) and MUST keep the user in the picker until a flash succeeds.
- **FR-017**: System MUST treat panic and CPU-lockup resets, and watchdog resets carrying the panic capture marker, as crashes: on the next boot it MUST write firmware version, reset reason, panic message, the last 16 log lines and a stack window to `/crash_report.txt`, MUST clear the marker only after a complete write, and MUST show a "System Crash" screen before Home.
- **FR-018**: System MUST run exactly one activity at a time on an activity stack, defer push/pop/replace until after the current activity's loop, render on one shared render task under one render mutex, apply night-mode polarity before each render, and go Home when the last stack entry is popped.
- **FR-019**: While a USB Drive session owns the SD card System MUST run only that activity and MUST suspend navigation, sleep, screenshots and serial commands.
- **FR-020**: System MUST re-render the current non-reader screen when USB power is connected or disconnected so the charging indicator updates.
- **FR-021**: Frontlight brightness and warmth MUST be restored at every boot; the light MUST be switched on only if it was on and Restore Light on Wake (`frontlightRestoreOnWake`, default On) is enabled or the boot is a silent restart.

#### B. Sleep screens

- **FR-022**: Sleep Screen (`sleepScreen`, default Dark) MUST offer Dark, Light, Custom, Cover, Cover + Custom, None, Quick Resume and Transparent in that order, rendered in normal polarity. Static screens MUST use a single half refresh (grayscale bitmaps add the grayscale passes after a half-refresh base; Quick Resume on X3 uses a fast differential refresh). A "Going to sleep" popup MUST precede every mode except Quick Resume; in Transparent mode the popup is shown but the retained frame is restored underneath it.
- **FR-023**: Custom MUST prefer `/sleep.bmp`, then a random valid BMP (header parses; names starting with '.' skipped) from `/.sleep/`, then `/sleep/`, re-drawing up to 20 times to avoid the last N shown indices of that kind where N = min(history fill, file count − 1) and the history holds 16 entries per kind (persisted in state.json), and MUST fall back to the default screen.
- **FR-024**: Cover MUST render the recorded open book's cover (EPUB, XTC, TXT sidecar, FB2) whenever a book path is recorded; Cover + Custom MUST render it only when sleep was entered from the reader and otherwise behave as Custom. Cover rendering MUST honour Sleep Screen Cover Mode (`sleepScreenCoverMode`: Fit default, Crop) and Sleep Screen Cover Filter (`sleepScreenCoverFilter`: None default, Contrast, Inverted), falling back to the Default screen (Cover) or Custom behaviour (Cover + Custom) when no book or cover is available.
- **FR-025**: Transparent MUST keep the current frame (materialising night-mode inversion), then prefer `/sleep-overlay.bmp`, `/sleep-overlay.png`, a random file from `/.sleep-overlay/`, then `/sleep-overlay/`; 32-bit BGRA BMPs with useful alpha and PNGs MUST be alpha-composited, other BMPs MUST treat white as transparent; it MUST fall back to the default screen.
- **FR-026**: Quick Resume MUST keep the current page on screen with a moon icon, save the raw framebuffer to `/.crosspoint/sleep_frame.bin`, and on the next splashless power-button wake restore it, delete the file and paint a loading icon over it (X3: differential fast refresh and a fast first reader refresh; other boards: half refresh); the reader then opens only under the FR-005 conditions, otherwise Home. A wrong-size frame file MUST be discarded on wake and a stale one deleted on the next non-Quick-Resume sleep.
- **FR-027**: Quick Resume on Timeout (`quickResumeSleepScreen`, default Off) MUST make auto-sleep use the Quick Resume presentation; it MUST be forced On while Sleep Screen is Quick Resume and revert when the user leaves that mode unless they set it explicitly.
- **FR-028**: Sleep image headers MUST be validated before any decode buffer is allocated for BMPs (planes 1, bpp 1/2/4/8/24/32, BI_RGB or 32-bit BI_BITFIELDS, palette ≤256, 1..2048 × 1..3072) and for PNGs found by directory scan (signature, IHDR, legal depth/colour type, ≤2048×3072 and ≤2048×1536 px, not interlaced, compression and filter 0); a root `/sleep-overlay.png` is validated by the shared decoder path instead (decoder allocated first; dimensions ≤32767 per side and ≤8 MP).

#### C. Home, library, file browser, recent books, image viewer

- **FR-029**: Home MUST show Browse Files, Recent Books, File Transfer and Settings in that order, MUST insert OPDS Browser after Recent Books only when at least one OPDS server is stored, MUST wrap selection, MUST accept swipes and taps on touch boards, and MUST place the cursor on the row of the screen just left when returning.
- **FR-030**: Home MUST show the most recently read existing book(s) as a Continue Reading tile (one; three in Lyra Extended; in RoundedRaff a cover-only tile plus the book title in the header band and a Continue Reading menu row) with cover thumbnail and title (author too in Classic and Lyra; Lyra Extended shows title only), and MUST open the most recent book when Back is released.
- **FR-031**: Home MUST generate missing cover thumbnails for recent EPUB, FB2 and XTC books after its first paint (with a progress popup), clear a book's stored cover path when generation fails, and cache the rendered tile so cursor moves do not re-decode it.
- **FR-032**: The file browser MUST list directories before files in natural order, show only `.epub`, `.fb2`, `.xtc`, `.xtch`, `.txt`, `.md`, `.bmp` and `.png` (only `.bin` in firmware-picker mode), hide dot-prefixed entries unless Show Hidden Files (`showHiddenFiles`, default Off) is on, and always hide "System Volume Information".
- **FR-033**: The file browser MUST show the current folder name in the header ("SD card" at root; "Select firmware file (.bin)" in firmware-picker mode) and the full path, left-truncated with an ellipsis, in a bottom band; folders MUST be shown as `[name]` in themes without file icons (Classic, RoundedRaff) and with an icon and bare name otherwise (Lyra, Lyra Extended).
- **FR-034**: A short Confirm release or tap MUST open the selected file (image viewer for .bmp/.png, otherwise the reader for the extension) or enter the folder with the cursor at the top; short Back MUST go up one level with the cursor on the folder just left (Home at root; cancel in picker mode); a Back hold of 1 s MUST jump to root in book-browser mode only (in the firmware picker a held Back does nothing, and any Confirm release on a `.bin` returns its path to the caller).
- **FR-035**: In book-browser mode (never in the firmware picker) a Confirm hold of 1 s or a touch long-press MUST offer to delete the entry; on confirmation directories MUST be removed recursively with each book's cache cleared first, and the cursor MUST be clamped afterwards.
- **FR-036**: List screens MUST step one row per navigation release (wrapping), page by the rows actually drawn while a navigation button is held, and scroll one page per vertical swipe without moving the selection.
- **FR-037**: Recent Books MUST persist up to ten entries (`/.crosspoint/recent.json`) most-recent-first with path, title, author and cover path; adding MUST prune missing files, de-duplicate and trim; the screen MUST prune on entry and offer removal on a Confirm hold or long-press; loading MUST tolerate a missing or wrongly typed list.
- **FR-038**: A finished EPUB (EpubReaderActivity only; the FB2, XTC and TXT readers do not implement this) MUST be removed from Recent Books while its end-of-book state is shown only when Clear Read Books from Recent List (`removeReadBooksFromRecents`, default Off) is on (and re-added if the reader leaves the end of the book), and MUST be moved to `/read/` when the reader is exited from the end-of-book state (suffixing " (N)" from N=2 on collision, re-keying its cache directory, updating recents and `openEpubPath` state; skipped when the book already lives under `/read/`) only when Move Finished Books to Read Folder (`moveFinishedToReadFolder`, default Off) is on.
- **FR-039**: The end-of-book suggestions MUST be up to three supported books from the same folder that sort strictly after the current file in natural order, respecting Show Hidden Files.
- **FR-040**: The image viewer MUST render BMP scaled to fit (or centred when smaller) and PNG scaled down but never up, step to sibling images with Left/Up, Right/Down or horizontal swipes, and return to the browser positioned on the image on Back.
- **FR-041**: The image viewer MUST offer Set Cover on Confirm: copy the BMP to `/sleep.bmp` and set Sleep Screen to Custom, or, when Sleep Screen is Transparent, copy the BMP or PNG to `/sleep-overlay.bmp`/`.png` keeping the mode; PNG MUST only be offered in Transparent mode.
- **FR-042**: Confirmation dialogs MUST default to Cancel and treat Back or an outside tap as cancel; option popups MUST show at most 16 options, wrap navigation, select on Confirm and dismiss on Back or an outside tap.
- **FR-043**: UI Theme (`uiTheme`, default Lyra) MUST offer Classic, Lyra, Lyra Extended and RoundedRaff, apply immediately and on leaving Settings, hide button hints on touch boards, follow the remapped button labels, and draw side hints in the board's edge layout.

#### D. EPUB parsing and caching

- **FR-044**: System MUST locate the package document via the `META-INF/container.xml` rootfile whose media-type is `application/oebps-package+xml` and fail the open when none is found; it MUST take the first `dc:title`, join all `dc:creator` values with ", ", record `dc:language`, resolve manifest hrefs relative to the OPF (URI-decoded and normalised), build the spine in itemref order skipping unknown idrefs, and, when the reader would otherwise open at spine index 0 (first open or progress saved in spine 0), open at the guide reference of type `start` when one exists (`text` references are ignored).
- **FR-045**: System MUST prefer the EPUB 3 nav document and fall back to NCX, continue without a TOC when neither parses, and assign each spine item the index of the first TOC entry pointing at it or inherit the previous one.
- **FR-046**: System MUST choose the cover from `<meta name="cover">` only when its manifest item has an `image/*` media-type, else the first item with the `cover-image` property, else from the guide reference of type `cover` or `cover-page`: the first `.jpg`/`.jpeg`/`.png` target among that page's `xlink:href="..."` attributes, then among its `src="..."` attributes; and MUST generate covers and thumbnails only from `.jpg`/`.jpeg`/`.png` hrefs, writing an empty thumbnail marker file when there is no cover or its extension is unsupported (a failed decode removes the output instead).
- **FR-047**: System MUST cache book metadata in `/.crosspoint/epub_<hash>/book.bin` (version 10) with spine and TOC lookup tables and cumulative uncompressed spine sizes, MUST delete a short write, and MUST reject and rebuild a cache whose version, header, table offsets or entries are inconsistent with the file.
- **FR-048**: Every `serialization::readString` call MUST reject a length prefix above 8 KB before allocating; the unbuffered `HalFile` reader used for `book.bin` and section files MUST additionally reject a length exceeding the bytes remaining in the file, while the buffered reader used only to stream the `spine.bin.tmp`/`toc.bin.tmp` build files applies the 8 KB cap alone.
- **FR-049**: Book progress MUST be computed from cumulative uncompressed spine bytes plus the fraction read of the current item.
- **FR-050**: With Embedded Style (`embeddedStyle`, default On) System MUST parse manifest stylesheets plus any `.css` discovered under the OPF directory, de-duplicate identical content, skip files over 128 KB or when free heap is below 64 KB (marking the result partial), cache rules in `css_rules.cache` (version 12) atomically with a partial flag, retry partial caches on later loads, and delete all section caches whenever the CSS cache content changes.
- **FR-051**: The CSS subset MUST support `tag`, `.class`, `tag.class` and comma groups; ignore selectors with combinators, attributes, ids, pseudo-classes or wildcards; cap storage at 1500 rules, 32 KB of selectors and 256 unique styles; interpret text-align, font-style, font-weight, text-decoration, text-indent, margin/padding (with shorthand), width, height, `display:none`, direction and vertical-align; resolve px, em/rem, pt and % lengths; clamp horizontal margins/padding to 2 em; apply the cascade element < class < element.class < inline; skip a BOM; treat quoted punctuation literally; and leave unparseable values undefined.
- **FR-052**: System MUST cache each laid-out spine item in `sections/<n>.bin` (version 45) whose header carries every render-affecting field (font, line compression, paragraph spacing, alignment, viewport, hyphenation, embedded style, image mode, focus reading); MUST rebuild on any mismatch or unknown version; MUST write the version byte last and commit via rename so an interrupted build is never valid; and MUST return no page rather than garbage for any out-of-file page.
- **FR-053**: System MUST build sections incrementally so the first pages appear before the chapter finishes, MUST suspend an unfinished build to a partial file with a byte watermark (keeping a larger existing partial), MUST estimate total pages from bytes consumed, and MUST drop a partial after a parse error.
- **FR-054**: System MUST cache the inflated chapter HTML in `html/<n>.html` keyed by book only, so re-pagination after a settings change skips inflation, retrying inflation up to three times.
- **FR-055**: Each page MUST record the visible-text codepoint offset of its first character (page 0 always records 0); a TOC anchor MUST start a new page — the break is applied when the anchor's element opens the next block (after the previous block is flushed) and only if the current page already has content; element ids (up to 1024 recorded per chapter, TOC anchors exempt, `<span>` ids excluded) MUST map to pages; paragraph and list-item indexes MUST map to pages.
- **FR-056**: Delete Book Cache MUST release the current section, remove the book's cache directory, recreate it, re-save the current progress (spine, page, chapter page count) and then return to the Home screen.

#### E. Layout, typography and images

- **FR-057**: Text MUST be tokenised with CJK characters as individual tokens, no-break spaces (U+00A0, U+202F) rendered as spaces that never break, U+2011 never breaking, tokens over 200 bytes split at UTF-8 boundaries, and paragraphs soft-flushed above 750 words (320 with embedded CSS).
- **FR-058**: Without hyphenation, line breaks MUST minimise the sum of squared trailing space (last line free, longer lines preferred on ties); with Hyphenation (`hyphenationEnabled`, default Off) lines MUST fill greedily and split the overflowing word at the widest legal prefix, inserting "-" only where required.
- **FR-059**: Hyphenation MUST use explicit-hyphen, soft-hyphen and apostrophe-contraction breaks (apostrophe only with ≥3 letters on each side) plus Liang patterns for the book's language among en, fr, de, ru, es, it, pl, sv, uk, fi (primary subtag of the dc:language tag; ISO 639-2 eng/fra/fre/deu/ger/rus/spa/ita/ukr/swe/fin mapped, no mapping for pol), MUST skip pattern hyphenation for other languages, for words over 68 codepoints or 158 bytes, and for words containing a codepoint outside the language's letter set, and MUST fall back to a break at every codepoint boundary (honouring the language's minimum prefix/suffix, 2/2 or 3/3 for English) only when no pattern or explicit break exists and only for the first word of a line (with hyphenation off, for any word wider than the line).
- **FR-060**: Paragraph Alignment (`paragraphAlignment`, default Justify) MUST offer Justify, Left, Center, Right and Book's Style (CSS text-align with justify default); headings default centred (a CSS text-align on the heading applies only when Embedded Style is on); justified text MUST distribute spare space evenly across word gaps except on the last line.
- **FR-061**: Extra Paragraph Spacing (`extraParagraphSpacing`, default On) MUST add half a line height after each paragraph; when off, naturally-aligned paragraphs (justified, or left-aligned LTR / right-aligned RTL) MUST get a three-space first-line indent unless CSS sets text-indent, in which case the CSS value is used; when on, only a negative CSS text-indent is applied.
- **FR-062**: Line Spacing (`lineSpacing`, default Normal) MUST offer Tight, Normal, Wide and Extra Wide as per-font compression factors; Screen Margin (`screenMargin`, 5..40 step 5, default 5) MUST inset the text area.
- **FR-063**: Words MUST be measured with kerning and space advances from the renderer, and SD-card font advances for the paragraph's codepoints MUST be pre-loaded before measuring.
- **FR-064**: Inline styles MUST compose bold, italic, underline, strikethrough, superscript (+40% ascender) and subscript (−25%) from tags and CSS; `<br>` MUST break without margins; an empty `<br>` MUST insert a blank line; `<hr>` MUST draw a centred rule; `display:none`, page-break roles, head/style/script/title/rp content MUST be skipped.
- **FR-065**: `<ruby>`/`<rt>` MUST render annotations above the base group with overhang accounting so ruby never exceeds the margins and groups never split across lines.
- **FR-066**: Simple tables (2..4 columns, cells ≤32 words/512 bytes, no colspan/rowspan/links, adequate width) MUST be laid out as positioned columns; anything else MUST fall back to stacked full-width paragraphs; nested tables MUST be flattened.
- **FR-067**: Paragraph direction MUST come from CSS `direction`/HTML `dir` (inherited) or, when a word in the paragraph begins with an RTL letter and no direction was set, be auto-detected from the first three words; lines MUST be reordered per UAX#9 (minibidi); RTL paragraphs whose alignment is Left without an explicit CSS text-align MUST be rendered right-aligned, while Justify and Center keep their alignment with justified RTL lines laid out from the right edge.
- **FR-068**: 253 named HTML entities MUST resolve; all text, titles and TOC labels MUST be composed to NFC; U+FEFF MUST be dropped.
- **FR-069**: Focus Reading (`focusReadingEnabled`, default Off) MUST bold roughly the first 45% (1..9 codepoints) of each alphabetic word, leave already-bold and CJK text unchanged, and be part of the section cache key.
- **FR-070**: Images (`imageRendering`, default Display) MUST offer Display, Placeholder ("[Image: alt]") and Suppress; images MUST scale to fit the container and viewport preserving aspect ratio, honour CSS width/height, centre, clamp the top margin so they never overflow the page, and page-break before an image that does not fit.
- **FR-071**: Image dimensions MUST be probed from the entry header at build time (rejecting ≤0, >32767 per side or >8 MP), images MUST be extracted lazily on first render, JPEG (baseline and progressive at DC-only) and PNG (8-bit and 1/2/4-bit gray/indexed, tRNS/alpha composited) MUST decode only above their heap gates, and other formats MUST fall back to alt text.
- **FR-072**: Decoded image pixels MUST be quantised to four grey levels with 4x4 Bayer ordered dithering and streamed to a 2-bit pixel cache in bounded row bands (≤24 KB); later passes MUST replay the cache, from RAM when heap allows (one image per page render, up to 96 KB in 16 KB chunks, released when the page render completes); an invalid cache MUST be re-decoded; up to 16 failed images per session MUST be remembered and drawn as an outline placeholder without re-decoding, with the list cleared when the reader is entered.
- **FR-073**: Internal links MUST be underlined and recorded per page (≤32 links, href ≤255 bytes) for touch hit-testing, and footnote entries (≤16 per page, label ≤31 chars) MUST be derived from link text.

#### F. Reader experience

- **FR-074**: System MUST refuse to open a path that does not exist and MUST dispatch `.xtc/.xtch`, `.fb2`, `.txt/.md` and all other files to the XTC, FB2, TXT and EPUB readers respectively (case-insensitive).
- **FR-075**: Opening an EPUB MUST record it as the open book and in Recent Books, load SD fonts, apply the saved orientation, show "Indexing" and disable the fast first refresh when the book has no metadata cache, and restore spine/page (accepting 4-, 6- and 10-byte progress records; page 0xFFFF means page 0).
- **FR-076**: Progress (spine, page, page count, visible-text offset) MUST be saved after a render whenever spine, page or page count changed, via a temp file renamed over `progress.bin`; values outside 0..65535 MUST be rejected.
- **FR-077**: Page-turn controls MUST be front Left/Right, side buttons per Side Button Layout (`sideButtonLayout`: Prev/Next default, Next/Prev, Disabled), touch per Touch Reader Controls (`touchReaderControls`: Off, Tap, Swipe default, Inverted Tap), tilt when enabled, and Power (forward only) in Page Turn mode; in Portrait 180° and Landscape CCW both the front Left/Right pair and the side page buttons MUST be swapped on touch boards or when Orient front buttons (`frontButtonFollowOrientation`, default Off) is on; turns MUST fire on press when Long-press button behavior is Off and on release or at 700 ms otherwise.
- **FR-078**: Long-press button behavior (`longPressButtonBehavior`, default Off) MUST make a ≥700 ms hold skip to the next chapter or back to the chapter start/previous chapter (Chapter skip), or rotate the orientation (Orientation change).
- **FR-079**: A page-turn request that arrives while a render holds the lock or within 200 ms of the previous turn MUST NOT execute immediately; it MUST be kept as a single pending turn (a newer request overwrites it) that fires on a later loop tick once the guard clears, and is dropped if the section is gone or the reader menu opens. Long-press chapter skips and orientation changes are not subject to the guard.
- **FR-080**: Background pagination MUST build two pages per loop tick while the section is partial or within five pages of the built frontier, only when free heap ≥32 KB and the largest block ≥16 KB, and MUST prewarm the next page's glyphs after 400 ms idle when heap allows.
- **FR-081**: The reader menu MUST open on Confirm release, a top-edge down swipe (only on boards without a frontlight; boards with one route that swipe to the frontlight panel first), a centre-third tap when Show Reader Menu (`showReaderMenu`, persisted as `tapForReaderMenu`, default Tap) is Tap, or a bottom-edge up swipe when it is Swipe Up (home-key boards); button-only boards MUST always get the full-screen list, touch boards MUST get the toolbar overlay when Reader Menu Style (`readerMenuStyle`, default List; touch boards seed Toolbar before settings load) is Toolbar and a section is loaded, and the list otherwise.
- **FR-082**: The toolbar overlay MUST offer a chapter scrub row, chapter title and page/percent line, and Contents, Text and More sheets, applying text changes live and restoring the clean page on close.
- **FR-083**: Reading Orientation (`orientation`, default Portrait) chosen in the menu MUST apply after the menu closes and persist; an orientation changed by another screen while the reader is stacked MUST be detected and reflowed.
- **FR-084**: Long-press Menu (`longPressMenuFunction`, default Disabled) MUST bind a Confirm hold to Bookmark (400 ms), Dictionary (400 ms), KOSync (1000 ms, only with credentials), Reader Menu (home-key boards) or nothing, and MUST run the same function on a Home-key hold.
- **FR-085**: Bookmarks MUST be stored per book at `/.crosspoint/bookmarks/<flattened path>.json` with XPath, percentage, a ≤72-character summary, spine/page hints and visible-text offset; toggling MUST remove a matching bookmark or insert a new one first; opening MUST resolve by offset, then page hints, then XPath/percentage; deleting MUST require Cancel/Delete confirmation.
- **FR-086**: Footnote and link navigation MUST push at most three return positions, restore the most recent on a short Back, save progress at the origin when the reader closes inside a footnote, and in Footnotes power mode open the only footnote directly or list several.
- **FR-087**: A tap on a link box (6 px slop, 28 px minimum width) MUST navigate before menu or page-turn zones are evaluated when touch controls are on.
- **FR-088**: Go to % MUST offer a 0..100 slider (±1 front buttons, ±10 side buttons/swipes, drag), map the percentage onto cumulative spine bytes, fully build that section and land on the proportional page.
- **FR-089**: Auto Turn MUST offer Off, 1, 3, 6 and 12 pages per minute, show "Auto Turn Enabled: N" in the status bar (reserving room when the bar is hidden), pause while rendering, and stop on Confirm/Back release, a menu gesture, an error, an empty chapter or the end of the book.
- **FR-090**: The status bar MUST compose chapter page/count, book percentage, title (chapter, book or none; "Unnamed" without a TOC entry), battery (percentage hidden per Hide Battery %), clock (RTC boards) and a progress bar per the status-bar settings, plus a bookmark flag and a building indicator.
- **FR-091**: Page turns MUST use a fast refresh and promote to a half refresh every Refresh Frequency (`refreshFrequency`: 1, 5, 10, 15 default, 30, Never) pages; a Power click in Refresh Screen mode MUST force the next render to a half refresh; an uncached open MUST not start with a fast refresh.
- **FR-092**: Short Back MUST go Home and a ≥1 s Back MUST open the file browser at the book's folder (swapped when Short Back to File Browser (`backShortToFileBrowser`, default Off) is on); left-edge back swipes MUST be ignored on the page.
- **FR-093**: The end-of-book screen MUST show "End of book"; when FR-039 yields at least one suggestion it MUST list them under "Continue with" plus a Home row, open the chosen book, and return to the last page on a short Back; with no suggestions it MUST show only the text, and a forward page turn MUST go Home while a backward one returns to the last page.
- **FR-094**: Error states MUST be shown as "Failed to index - invalid book" (build failure), "Page load error" once a page load has failed four times in a row (each of the first three failures clears the section cache and rebuilds it), "Empty chapter" and "Out of bounds".
- **FR-095**: Text Settings opened from the reader MUST keep the section loaded while open; on return the reader MUST remember the current page's visible-text offset, release the section, and rebuild it with the new settings at that offset on the next render (list menu: after the reader menu closes; toolbar Text panel: under the re-opened panel).

#### G. TXT, XTC and FB2 formats

- **FR-096**: The TXT reader MUST paginate by streaming 8 KB chunks, splitting on LF (ignoring CR), wrapping at the last space or a UTF-8 boundary, computing lines per page from the font, margin and status bar, and MUST cache page offsets in `txt_<hash>/index.bin` keyed on file size, viewport, lines per page, font, margin and alignment, rebuilding behind an "Indexing" popup on mismatch.
- **FR-097**: The TXT reader MUST render Left, Center or Right per the alignment setting (Justified as Left), right-align lines starting with RTL text when the alignment is Left or Justified (Center is kept), apply anti-aliasing when enabled, show "Empty file" for no pages, skip ten pages on a ≥700 ms page-turn hold when Long-press button behavior is Chapter skip, and save the page (clamped on restore) after every render; `.md` MUST be read as plain text and the TXT title MUST strip only `.txt`.
- **FR-098**: TXT covers MUST be discovered as `<basename>.{bmp,jpg,jpeg,png}` then `cover/Cover/COVER.*` in the same folder; BMP MUST be copied, JPEG converted, PNG rejected.
- **FR-099**: The XTC reader MUST accept only containers with magic `XTC\0` (1-bit) or `XTCH` (2-bit), version 1.0 or 0.1, a non-zero page count and a page table that fits the file; MUST read title/author only when the header says so (an empty title falls back to the file name without extension; the author has no fallback); MUST read page-table entries on demand; MUST validate each page header magic; and MUST refuse a page larger than its buffer.
- **FR-100**: XTC chapters MUST be parsed lazily from 96-byte records when the header declares them and the layout allows (chapter offset read as 4 bytes), dropping invalid ranges and clamping ends; Confirm or the touch menu gesture MUST open chapter selection only when chapters exist.
- **FR-101**: XTC pages MUST render in portrait regardless of the orientation setting, 1-bit pages by direct blit and 2-bit pages by a four-pass grayscale sequence; XTC Status Bar (`xtcStatusBarMode`: Hide default, Bottom, Top) MUST overlay the status bar on 1-bit pages only, with chapter-relative numbering inside chapters; "Memory error" and "Page load error" MUST be shown on allocation or read failure; progress MUST be saved as a 4-byte page index.
- **FR-102**: XTC covers MUST come from page 0 (4-grey palette BMP for XTCH, 1-bit for XTC) and thumbnails MUST be 1-bit, area-averaged and dithered, never upscaled (a page already no larger than the thumbnail target is copied from `cover.bmp` unchanged, so an XTCH thumbnail is then the 4-grey BMP).
- **FR-103** *(fork-only)*: The FB2 reader MUST extract title, first author, language and cover id, decode windows-1251/1252 documents, treat only depth-1 sections of the first (and later unnamed) bodies as chapters, fall back to a single whole-file section, and cache metadata in `fb2_<hash>/book.bin` (version 2) with bounded, validated reads.
- **FR-104** *(fork-only)*: FB2 sections MUST be laid out with the EPUB layout pipeline (title centred bold, subtitle italic, epigraph right/indented italic, cite indented, poem/stanza centred, empty-line gap, image and table placeholders) into `sections/<n>.bin` (version 4) keyed on the render settings, rebuilt on mismatch, with "Indexing" for sections ≥50 KB and at most three rebuild retries.
- **FR-105** *(fork-only)*: The FB2 reader MUST save section/page/page-count progress (6 bytes, accepting 4), rescale the page after re-pagination, support Select Chapter, Text Settings, Go to % (by section byte weight), orientation, Go Home and Delete Cache, and extract the cover by streaming the referenced base64 binary.
- **FR-106**: Every format MUST write progress through a temp file renamed into place, and cache directories `epub_`, `fb2_`, `txt_` and `xtc_` MUST be recognised as book caches for clearing.

#### H. Rendering and fonts

- **FR-107**: All drawing MUST go through one 1-bpp framebuffer sized from the active panel (48,000 bytes for the 800×480 X4/X4 Pro/X4 Classic panel; 52,272 bytes for the 792×528 X3) in logical coordinates for four orientations (portrait = panel-height × panel-width, landscape = panel-width × panel-height, i.e. 480×800 / 800×480 on X4), MUST drop pixels outside the panel, and MUST expose the board profile's bezel insets rotated into the current orientation.
- **FR-108**: Refresh modes MUST be Full, Half and Fast; a one-shot promoted refresh MUST apply to the next paint; X3 MUST resync the controller before half refreshes; a non-seamless boot whose wake reason is power button, after-flash or other MUST request a controller resync on every panel driver (X3: forced full sync; X4 family: full clear), and a seamless boot MUST skip the initial resync instead.
- **FR-109**: Night Mode (`screenInverted`, default Off) MUST invert output polarity per render without changing the framebuffer, MUST keep content images in their original polarity, and MUST leave the sleep screen normal.
- **FR-110**: Sunlight Fading Fix (`fadingFix`, default Off; hidden on touch boards such as the X4 Pro, shown on button boards including the X4 Classic) MUST pass the turn-off-screen flag to every refresh and MUST force blocking refreshes (no async overlap).
- **FR-111**: With Text Anti-Aliasing (`textAntiAliasing`, default On) page text MUST render as a black-and-white base plus two grey planes; strip-capable panels MUST render planes in 80-row bands into a scratch strip (whole-plane buffers with async overlap when heap allows), other panels and inverted mode MUST snapshot the frame in 8,000-byte chunks and restore it; allocation failure MUST skip anti-aliasing for the page.
- **FR-112**: Text drawing MUST apply ligature pairs (not to Arabic presentation forms), snap advance-plus-kern as one 12.4 fixed-point step, overlay combining marks on the preceding base (Hebrew niqqud with native anchors), render superscript/subscript at 50% with halved advance, skip CJK kerning, substitute U+FFFD for missing glyphs, and fall back bold-italic → bold → italic → regular.
- **FR-113**: Built-in fonts MUST be Noto Serif and Noto Sans at 12/14/16/18 pt in four styles, Ubuntu UI 10/12 pt regular and bold, and Noto Sans 8 pt, registered under non-zero hashed ids; glyph bitmaps MUST be decompressed per page into at most four 512-glyph slots with a hot-group fallback, skipping a glyph rather than aborting on allocation failure.
- **FR-114**: Reader pages MUST be pre-scanned in a record-only pass and their glyphs pre-warmed in one batch per font and style before drawing.
- **FR-115**: SD fonts MUST be discovered under `/.fonts/<Family>/` (preferred) and `/fonts/<Family>/` as `<name>_<size>.cpfont` (size 1..255), hidden root winning on collisions, skipping names starting with "." or "_", rejecting duplicate sizes, sorted and capped at 128 families; installs MUST go to the family's existing root, else `/.fonts` when it exists, else `/fonts` when only it exists, else a newly created `/.fonts`.
- **FR-116**: Only `.cpfont` files with magic `CPFONT\0\0`, version 4, 1..4 styles, counts within caps and a strictly ascending contiguous interval table MUST load; ids MUST be an FNV-1a hash of the 32-byte file header and style TOC entries, continued over the family name and point size (0 remapped to 1); an id already registered with the renderer MUST be refused.
- **FR-117**: One SD file MUST be loaded for the reader at the nearest installed size (ties to smaller, snapped size persisted); glyphs MUST be fetched per page into retained mini arenas (kept while free heap ≥40 KB, released after repeated under-use), with an 8-slot overflow ring, a 768-entry advance table for layout and a per-page kern matrix; the registry MUST be re-scanned when marked dirty by web uploads or deletes; a missing family MUST clear the selection.
- **FR-118**: When the selected SD family covers CJK (probe of U+4E00/U+3042/U+30A2/U+AC00 in the loaded reader-size font), it MUST also load the family's 8, 10 and 12 pt files, where present, as fallbacks for the small/UI-10/UI-12 fonts, and a UI string containing a CJK codepoint the primary font lacks but the fallback covers MUST be measured and drawn entirely with the fallback.
- **FR-119**: Text Settings MUST list Noto Serif (default), Noto Sans and every SD family in the Font tab and the active family's sizes in the Size tab (12/14/16/18 pt for built-ins; `fontSize` stored as a point size, default 14, legacy slots migrated, legacy OpenDyslexic mapped to the SD family), applying changes under the render lock and saving immediately.
- **FR-120**: Manage Fonts MUST connect Wi-Fi, fetch the version-1 manifest from the crosspoint-fonts release to a temp file, reject other versions and malformed entries, cap script groups at 32, show a group list (when the manifest declares script groups) then a family list with Download All/Update All rows and Installed/Update status (by file size), download files with CRC32 and magic verification, delete the family on abort or failure, offer deletion of installed families, and disconnect and restart silently on exit.
- **FR-121**: BMP rendering MUST accept 1/2/4/8/24/32 bpp BI_RGB or 32-bit BI_BITFIELDS up to 2048×3072 with ≤256 palette entries, map 1/2-bpp images and palettes within ±21 of the four native grey levels directly, dither other palettes with Atkinson diffusion only when the caller opts in (BMP viewer and sleep images; theme covers use fixed-threshold quantisation), and take a 1-bit fast path when no crop is requested.
- **FR-122**: Strings containing Hebrew or Arabic lead bytes MUST be reordered per UAX#9 and shaped (Lam-Alef, Perso-Arabic letters, ZWJ/ZWNJ, transparent marks) on a bounded 128-codepoint buffer; longer lines MUST be drawn unprocessed.
- **FR-123**: UTF-8 decoding MUST return U+FFFD for invalid, overlong, surrogate or out-of-range sequences, and NFD Latin/Vietnamese/Cyrillic sequences (base + U+0300–U+036F marks) MUST be precomposed when EPUB/FB2 words are parsed for layout and when EPUB titles and TOC entries are read, so they render precomposed.
- **FR-124**: The framebuffer storage MUST be lendable in place to chapter builds and restored (white) afterwards; region snapshot/restore MUST support partial repaints.

#### I. Settings model and web settings API

- **FR-125**: `CrossPointSettings` MUST persist as one flat JSON object at `/.crosspoint/settings.json`, loaded at boot before language, theme and frontlight are applied, driven by a single settings table (`getSettingsList()`) that also feeds the on-device Settings screen and the web API; front-button roles, font family/size, SD font and dictionary names, language and keyboard layouts are written to the same file outside the table loop, and the table's KOReader Sync entries persist to the KOReader credential store's own file instead.
- **FR-126**: On load, enum values MUST be validated against the option count and toggles against 0/1, an out-of-range value keeping the in-memory value; value-type settings MUST be clamped to their min/max; missing keys MUST keep current values except that font family/size and front-button roles reset to defaults and SD font/dictionary names are cleared; string fields MUST be truncated and NUL-terminated; obfuscated strings MUST be read from `<key>_obf` with plaintext fallback (a plaintext or oversized obfuscated value requests a re-save).
- **FR-127**: Legacy files MUST be migrated and re-saved: `sleepTimeout` enum to minutes, font-size slots 0..3 to point sizes 12/14/16/18, the OpenDyslexic built-in index (2) to the SD family `OpenDyslexic` when no SD font name is stored (any other out-of-range built-in index falls back to Noto Serif); duplicate front-button roles MUST be reset in memory to the identity mapping without requesting a re-save; the language MUST be stored as a code string with English fallback; keyboard layouts MUST be omitted while unconfigured.
- **FR-128**: The Settings screen MUST show Display, Reader, Controls and System tabs, reset to Display on entry, toggle two-option settings in place, open a picker for larger enums, step values with wrap, save every change immediately, and save on Back from the tab bar.
- **FR-129**: Board capabilities MUST prune entries: touch boards hide Remap Front Buttons, Orient front buttons, Sunlight Fading Fix and Short Back to File Browser (Sunlight Fading Fix is also hidden on the X4 Pro and the non-touch X4 Classic); button boards hide Touch Reader Controls and Reader Menu Style; Show Reader Menu needs a Home key; Tilt Page Turn needs an IMU; Restore Light on Wake exists only in frontlight builds; Short Power Button Click offers Confirm only in touch builds and the long-press Confirm "Reader Menu" option only with a Home key; clock rows need an RTC; Quick-return from footnotes shows only while Short Power Button Click is Footnotes; Dictionary shows only when a dictionary exists.
- **FR-130**: Customise Status Bar MUST edit chapter page count (default Show), book percentage (Show), progress bar (Book/Chapter/Hide, default Hide), thickness (Thin/Medium/Thick, default Medium), title (Book/Chapter default/Hide), battery (Show), XTC bar, and on RTC boards clock position (Hide/Right/Left), format (24h default/12h), UTC offset (quarter-hour steps, −12:00..+14:00, `:00` only at the ends) and a manual NTP sync; corrupt values MUST be clamped on entry and a live preview shown.
- **FR-131**: Hide Battery % (`hideBatteryPercentage`, default Never) MUST offer Never, In Reader and Always.
- **FR-132**: Clear Reading Cache MUST require confirmation, delete only book-cache directories under `/.crosspoint`, and report removed and failed counts.
- **FR-133**: `GET /api/settings` MUST stream every keyed setting as `{key, name, category, type, value, options | min/max/step}` with localised labels and runtime-built font-family and font-size options (string entries carry only `value`; category-less entries report the "None" label; the dictionary entry has no key and no dictionary list is passed, so it is not exposed); `POST /api/settings` MUST apply a partial JSON object with enum/value range validation (toggles coerce any non-zero to 1, strings are truncated to the field size), save `settings.json` once (KOReader entries also save their own store) and reply "Applied N setting(s)"; settings without a device category (frontlight state, OPDS folder/format) MUST be web-editable but hidden on device.

#### J. Controls, input and the frontlight

- **FR-134**: Logical Back/Confirm/Left/Right MUST resolve through the persisted remap; Up, Down and Power MUST never be remapped; PageBack/PageForward MUST follow Side Button Layout; NavNext/NavPrevious MUST combine side Down/front Right and side Up/front Left; the navigation axis MUST flip only when (the board has touch or Orient front buttons (`frontButtonFollowOrientation`, default Off) is on) and the live renderer orientation is inverted or landscape-CCW.
- **FR-135**: The remap wizard MUST capture raw front-button presses for Back, Confirm, Left and Right in order, reject an already-assigned button, save after the fourth, reset to defaults on Side Up and cancel on Side Down; button hints MUST follow the mapping.
- **FR-136**: Long presses MUST fire once per hold at their threshold and suppress the following release; suppressed releases MUST be consumed centrally before activity input.
- **FR-137**: On touch boards a left-edge right swipe MUST act as Back, a bottom-edge up swipe or Home-key tap as Home, a top-edge down swipe as the menu gesture (or the light panel when a frontlight exists), and a tap on the top 44 px of Home, Browse Files, Settings or File Transfer MUST open the control center.
- **FR-138**: Tilt Page Turn (`tiltPageTurn`, default Off; Normal/Inverted) MUST trigger forward/back when the orientation-mapped gyro rate exceeds 270°/s, require a return below 50°/s and a 600 ms cooldown, ignore the first 300 ms after wake, poll at 20 Hz only inside a reader, count as activity for auto-sleep, and stand the IMU by when off or before sleep.
- **FR-139**: The control center MUST clamp brightness to 1..100 (adjusting turns the light on), warmth to 0..100 on warm-light boards, offer a lamp toggle, and on touch boards Night Mode, Refresh (promote a full refresh and close), Orientation (cycle) and Touch on/off tiles; brightness and warmth MUST always persist, the on/off state only when the user changed it.
- **FR-140**: Text entry MUST open on the UI language's layout when it is enabled, otherwise on the next enabled layout (a UI language without a layout table opens on EN; URL fields on an EN URL layout with snippets), show a language key only when more than one layout is enabled, support Shift, symbols, backspace, long-press clear, a cursor mode, password masking with a reveal toggle, a maximum length, and edit by whole codepoints; Keyboard Layouts MUST keep at least one Latin layout enabled.
- **FR-141**: Time to Sleep MUST be edited with an interval picker (small step 1, large step 5, "Never" at the maximum).

#### K. Localisation

- **FR-142**: Every user-facing string MUST resolve through generated string tables built from one YAML file per language (34 languages), English being the key reference; missing keys MUST fall back to English at build time; strings identical to English MUST be shared; unused keys MUST be stripped in firmware builds; a code reference to a missing key MUST fail the build.
- **FR-143**: The Language list MUST show native names, English first then alphabetical by BCP 47 tag, apply immediately and persist the language code.
- **FR-144**: Keyboard layouts MUST be available for English, French, German, Spanish, Russian, Ukrainian, Belarusian, Kazakh and Hebrew, persisted as a bitmask by table position.

#### L. Network and file transfer

- **FR-145**: File Transfer MUST offer Join a Network, Calibre Wireless and Create Hotspot, plus USB Drive on boards built with USB mass-storage support; cancelling MUST return Home; a cancelled Wi-Fi selection MUST return to the mode menu.
- **FR-146**: Wi-Fi selection MUST auto-connect to the last-connected saved network (7 s), then visible saved networks strongest-first, let Confirm show the list and Back cancel, omit hidden-SSID results, merge duplicates keeping the strongest, sort saved first then by signal, append "Add hidden network..." (SSID ≤32), prompt for passwords (≤64) on encrypted networks, time out manual connections after 15 s with a reason, offer to save a newly typed password (default Yes) and to forget a failing saved network (default Cancel), record the last-connected SSID, set the hostname `CrossPoint-Reader-<MAC>`, and sync the RTC from NTP on the first connection when a clock exists.
- **FR-147**: Up to eight credentials MUST persist in `/.crosspoint/wifi.json` with passwords XOR-obfuscated against the device MAC, base64-encoded and guarded by a stored length (≤64) and CRC32; entries whose decoded length or CRC32 mismatches, or whose password exceeds 64 bytes, MUST be discarded; legacy plaintext passwords and entries lacking the length/CRC32 fields MUST be accepted and the file rewritten in the obfuscated form immediately after loading.
- **FR-148**: Create Hotspot MUST start an open access point "CrossPoint-Reader" (channel 1, four stations) with a wildcard DNS server, and show a join QR, the SSID, a URL QR and the IP; in AP mode unmatched non-API URLs MUST redirect to `/`.
- **FR-149**: The web server MUST serve HTTP on port 80 (gzip-compressed pages `/`, `/files`, `/settings`, `/fonts`, `/js/jszip.min.js`), WebSocket uploads on port 81, UDP discovery on 8134 (answering "hello" with `crosspoint (on <host>);81`), mDNS `crosspoint.local`, CORS headers on every response, and MUST refuse to start without a station or AP link.
- **FR-150**: `GET /api/status` MUST return version, IP, mode, RSSI, free heap, uptime, device name and serial; `GET /api/files` MUST stream a directory listing honouring Show Hidden Files and always hiding "System Volume Information" and "XTCache"; `GET /download` MUST stream a file with the EPUB media type where applicable and refuse dot-prefixed and protected names.
- **FR-151**: `POST /upload` (multipart, 4 KB buffered) and the WebSocket protocol (`START:<name>:<size>:<path>` → `READY`, binary frames, `PROGRESS:<recv>:<total>` every 64 KB or at the end, `DONE` or `ERROR:<msg>`) MUST refuse an existing destination and clear the book cache for the path; the WebSocket path MUST allow one upload at a time, accept binary frames only from its owning client, complete zero-size uploads immediately, and delete the partial file on client disconnect, overflow or write error; the multipart path MUST delete the partial file only on an aborted upload (a write error closes the file and returns 400, leaving the partial file on the card).
- **FR-152**: `POST /mkdir`, `/rename` (files only, 409 on collision, cache cleared first), `/move` (files only, into an existing folder) and `/delete` (single path or JSON array; files and empty folders; per-item failure reasons) MUST be provided; `/rename` and `/move` MUST normalise paths.
- **FR-153**: `GET`/`POST /api/opds` and `/api/wifi` (plus `/delete` variants) MUST list entries with a `hasPassword` flag, never return passwords, add or update by index, preserve the stored password when the field is omitted, and enforce the eight-entry limits.
- **FR-154**: `GET /api/fonts`, `POST /api/fonts/upload` and `POST /api/fonts/delete` MUST list families with sizes and files, validate family name, `.cpfont` filename and `CPFONT` magic before accepting an upload (deleting invalid files), delete a family (clearing the active selection), and mark the font registry dirty.
- **FR-155**: The web File Manager MUST offer browsing, multi-select, upload with automatic " (2)" suffixing and HTTP fallback, optional rename from OPF metadata, an optional client-side EPUB optimiser (device profile, JPEG quality, grayscale, crop, split), folder creation, rename, move, delete and download links; the Settings page MUST load settings, Wi-Fi and OPDS cards sequentially; the Fonts page MUST upload one family at a time.
- **FR-156**: WebDAV (Class 1) MUST support OPTIONS, PROPFIND (Depth 0 or 1; missing/infinity treated as 1), GET, HEAD, PUT (temp `.davtmp` file then rename), DELETE (files and empty folders), MKCOL, MOVE, COPY (files only) and dummy LOCK/UNLOCK, honouring the Overwrite header (default T; `F` → 412); MUST normalise paths (a path containing a backslash normalises to the root rather than being rejected with an error), reject URIs or Destination headers decoding to NUL (400), refuse dot-prefixed and protected segments anywhere in the path, and report a fixed modification date (`Thu, 01 Jan 2024 00:00:00 GMT`).
- **FR-157**: Calibre Wireless MUST run the same server in station mode, show instructions, "Receiving:" progress and a "Received:" toast, and rely on UDP discovery plus the WebSocket protocol.
- **FR-158**: USB Drive MUST hand the raw SD card to the host on USB-OTG boards (`FREEINK_CAP_USB_MSC`), show Preparing, Waiting, Connected and error states, time out the host wait after five minutes (Back, Power or the Home gesture also exit while waiting), show a start-failure message that times out after 30 s, prevent auto-sleep while connected or recovering from an I/O error, recover from I/O errors by requesting a host disconnect (forcing a reboot after 1 s), and reboot to Home when the host ejects or disconnects the drive.
- **FR-159**: While the server runs System MUST prevent auto-sleep, skip the loop delay, pump input inside the request loop, release SD font caches before allocating Wi-Fi, tolerate link loss for five minutes, show a 0..4 bar signal indicator with hysteresis, and restart silently to Home on exit (touch boards: Wi-Fi off in place).
- **FR-160**: The shared HTTP client MUST follow at most five redirects, use a 60 s timeout, send a `CrossPoint-ESP32-<version>` user agent, send Basic auth only when both credentials are present, disable Wi-Fi power save during transfers, delete a destination file on failure or zero bytes, and optionally downgrade redirect targets to plain HTTP for font downloads.

#### M. OPDS catalogs

- **FR-161**: Up to eight OPDS servers (name, URL, username, password) MUST persist in `/.crosspoint/opds.json` with obfuscated passwords, legacy plaintext accepted and rewritten, and missing or malformed lists treated as empty.
- **FR-162**: The server list MUST show servers, Add Server, Download folder (`opdsDownloadFolder`, default SD root, normalised to `/path`) and Filename format (`opdsFilenameFormat`: Author - Title default, Title - Author, Title); the editor MUST save each field immediately (name/username/password ≤63, URL ≤127, a bare scheme treated as empty), offer Delete Server for existing entries, and reload from disk when returning.
- **FR-163**: The browser MUST connect Wi-Fi first, fetch feeds with Basic auth, parse them incrementally keeping at most 62 entries with bounded strings, classify entries as books (EPUB acquisition link, preferring hrefs containing `.epub` or `/epub/`) or navigation, add "« Previous Page"/"Next Page »" rows from top-level links, offer search when an OpenSearch template with `{searchTerms}` exists, resolve hrefs relative to the feed URL, and show retryable errors ("No server URL configured", "Failed to fetch feed", "Failed to parse feed", "No entries found").
- **FR-164**: Downloads MUST ensure the folder (falling back to root), name the file per the format setting with FAT-safe sanitisation (≤100-byte stem, "book" fallback, `.epub`), release the catalog and SD font caches, refuse below 40,000 bytes free or a 20,000-byte largest block, show progress (repainted every 5% or 5 s) with Cancel, delete partial files on abort, overwrite an existing file, treat zero bytes as failure, clear the book cache and reload the feed.

#### N. Firmware updates

- **FR-165**: Check for updates MUST connect Wi-Fi, stream the latest-release JSON from the GitHub API through a bounded streaming parser (tag ≤31, URL ≤511, tokens ≤511, nesting ≤32, asset sizes 0..2^32−1), select `firmware.bin` for the combined X4/X3 build or `firmware-<board>.bin` otherwise, compare semantic versions (leading "v" stripped, non-semver never newer, "-rc" builds upgrade to the same numeric release), and show "No update available", "Update failed" or the current/new versions with Cancel/Update (default Update).
- **FR-166**: OTA install MUST stream into the inactive OTA partition, abort on a chip id mismatch in the first 14 bytes or a foreign `CROSSPOINT-BOARD-V1:<board>;` tag (untagged images accepted), disable Wi-Fi power save during the transfer, report progress per whole percent (repainting every 2%), verify the image, select it as boot partition only on success, restart after a 3 s "Update complete", and show "Firmware is for a different device" when rejected.
- **FR-167**: SD Card Firmware Update MUST open a `.bin`-only picker, validate the file (openable, ≥64 KiB, ≤partition size, magic 0xE9, chip id, segment table within EOF, XOR checksum, SHA-256 trailer, exact padded size, board tag), ask "Update firmware?", re-validate at flash time, write raw with interleaved 64 KiB erases and per-percent progress, switch the bootloader selection by writing a new otadata entry, and restart after 1.5 s; failures MUST be named ("Cannot open file", "Firmware too large for partition", "Firmware file is too small", "Firmware is for a different device", "Invalid firmware file", "Firmware write failed").
- **FR-168**: Every firmware image MUST embed exactly one board tag derived from the device build flag, and release assets MUST be named as the updater requests them.

#### O. KOReader progress sync

- **FR-169**: KOReader Sync settings MUST offer Username (≤64), Password (≤64, masked in the row), Sync Server URL (≤128; empty = `https://sync.crosspointreader.com`; a bare scheme = empty), Document Matching (Filename default, Binary), Send Document Metadata (Off default), Sync Behavior (Ask every time / Smart sync; Smart for fresh configurations, Ask for migrated files), Sign Up and Authenticate (disabled until credentials exist), saving every change to `/.crosspoint/koreader.json` (config version 2, obfuscated password; pre-v2 files that have credentials and no explicit server URL are pinned to `https://sync.koreader.rocks:443`).
- **FR-170**: Authenticate, fetch and upload requests MUST carry `Accept: application/vnd.koreader.v1+json`, `x-auth-user`, `x-auth-key` (MD5 of the password) and Basic authorisation; sign-up MUST instead POST the username and MD5 password as a JSON body with only the `Accept` and `Content-Type` headers. Every request MUST be refused below 35,000 bytes free or a 20,000-byte largest block, MUST treat any 2xx as success, 401 as authentication failure (authenticate, fetch, upload), 204/404 as "no progress" on fetch and 402 as "user exists" on sign-up.
- **FR-171**: Document ids MUST be MD5 of the file basename (Filename) or KOReader's partial MD5 over 1 KiB samples at 0, 1024, 4096 … 2^30 (Binary).
- **FR-172**: Uploads MUST send document, progress (KOReader xpointer), percentage, device "CrossPoint" and id "crosspoint-reader", optional metadata (filename, title, authors), and, only for the CrossPoint server, a position extension (pctQ, spine, page, pages, paragraph, xpath ≤120 bytes).
- **FR-173**: The upload xpointer MUST be built from the page's paragraph index as an element ancestry path with 1-based DocFragment, or from the intra-chapter fraction as a `text()[n].c` codepoint offset, or by byte-level paragraph counting when the chapter is not well-formed; chapter starts MUST be `/body/DocFragment[N]/body`.
- **FR-174**: Incoming xpointers MUST be resolved to a visible-text codepoint offset by streaming the chapter (strict ancestry, relaxed wrapper retry, bare body text, legacy `/p[N]`, chapter-start forms), converted to a page via the section cache, else mapped by percentage refined by list-item, anchor and paragraph tables; ancestry parse failures (indices over INT_MAX, tags longer than 11 chars) MUST fall back to the legacy `/p[N]`, chapter-start and percentage paths, and ancestries deeper than 16 steps MUST be truncated to their first 16 steps; the position extension MUST be used only when the xpointer cannot be resolved.
- **FR-175**: Sync from the reader MUST save progress first (aborting with "Could not save progress" on failure), pre-compute the local position and chapter name, release the section and book, and after completion return to the book; Smart sync MUST probe the alternate document id and adopt the furthest record, upload when nothing is stored, report "Already synced" within 0.001, upload under the primary id when local is ahead and apply remote otherwise; Ask mode MUST show the comparison with Apply remote/Upload local (preselecting the furthest) or the "No remote progress found" upload prompt.
- **FR-176**: Applying remote progress MUST write spine, page and (when in that spine) the visible-text offset to the book's progress file; the upload-success and "Already synced" screens MUST auto-return after 1.2 s (failure and no-credentials screens wait for Back/Confirm); Wi-Fi modem sleep MUST be off during sync, the radio stopped after an upload, and the device restarted silently on exit whenever credentials existed and the Wi-Fi path was entered (even if Wi-Fi selection was cancelled).

#### P. Dictionary lookup

- **FR-177**: Dictionaries MUST be discovered as non-dot subfolders of `/dictionaries/` or `/.dictionaries/` containing exactly one `.idx` stem and a `.dict` or `.dict.dz`, listed case-insensitively; the Dictionary setting (`dictionaryName`, ≤31 chars, None default) MUST appear only when at least one exists; folders whose `.ifo` declares 64-bit index offsets MUST be rejected when the dictionary is opened for lookup (they are still listed; the lookup reports "Dictionary error").
- **FR-178**: Lookup MUST start from the reader menu, or, when Long-press Menu is Dictionary, from a 400 ms Confirm hold or a Home-key hold, and MUST show "No dictionary set" for 2.5 s when none is selected.
- **FR-179**: Word selection MUST highlight the word nearest the centre of the middle row, step words with Left/Right (auto-repeat), rows with Up/Down, look up on Confirm or tap, move on a held touch, return on Back, and repaint only the highlight boxes.
- **FR-180**: The first lookup in a session MUST check the sampled-offset sidecars (`.qidx` over `.idx`, `.sidx` over `.syn`; one offset per 256 entries, source size validated, zero header until complete) and rebuild stale ones behind "Indexing dictionary..." while yielding every 64 KB.
- **FR-181**: Lookup MUST clean the token (edge punctuation, general-punctuation codepoints, ASCII lower-casing), then try an exact match (sidecar bisect plus ≤256-entry scan), synonyms (ordinal resolved via the index sidecar; skipped with misses reported as read errors when the synonym sidecar is unusable), then English stem variants, cap definitions at 64 KB, require 8 KB heap headroom, decompress dictzip chunks on demand into a temp file, and classify outcomes as Found, Not found, Low memory, Decompress or Read error with distinct 1.5 s popups.
- **FR-182**: The definition screen MUST show the headword, a page counter (when there is more than one page) and the body in the reader font; HTML dictionaries (`sametypesequence=h`, definition ≤16 KB, heap ≥40 KB/20 KB block) MUST be normalised to XHTML and laid out with the reading engine (≤64 pages, ≤512 elements), falling back to plain text with greedy wrapping; pages MUST turn with NavNext/NavPrevious or taps and Back MUST return to word selection.

#### Q. Diagnostics, storage and platform

- **FR-183**: All SD access MUST go through the storage HAL (`HalStorage`/`HalFile`), whose operations MUST be serialized by one recursive mutex (`begin()`/`ready()` run unlocked at setup, and the in-memory accessors `size`/`fileSize`/`isDirectory`/`isOpen` forward without the lock); file handles MUST close under the lock; the SDK's `SdMan` convenience macro MUST be undefined by the HAL header and no non-HAL code MUST reference `SDCardManager` directly.
- **FR-184**: Firmware state MUST live under `/.crosspoint/` (settings, state, recent, OPDS, Wi-Fi, KOReader stores; sleep frame; dictionary temp files; bookmarks; per-book caches keyed by a hash of the path); JSON stores MUST be created on first write, missing files treated as first boot, and unparsable files logged and ignored.
- **FR-185**: Logging MUST format `[ms] [LVL] [ORIGIN] message` at Error/Info/Debug levels gated by build flags, MUST retain the last 16 lines in RTC memory guarded by a magic word for crash reports, and MUST route through the ROM console on Sticky.
- **FR-186**: A serial `CMD:SCREENSHOT` line MUST be answered with `SCREENSHOT_START:<size>`, the raw framebuffer and `SCREENSHOT_END`; heap statistics MUST be logged every 10 s while serial is connected.
- **FR-187**: Screenshots MUST be saved as 1-bit bottom-up BMPs rotated 90° to portrait under `/screenshots/` (`screenshot-<ms>.bmp` outside a reader or when the title sanitizes to empty; `<title>/<title>_ch<n>_p<page>_<pct>pct_<ms>.bmp` in the EPUB reader with a known spine index, `<n>` being 1-based; `<title>/<title>_p<page>_<pct>pct_<ms>.bmp` in other readers), with the title FAT32-sanitized to ≤63 bytes and the full path truncated to ≤255 bytes on a UTF-8 boundary, a 1 s border flash, and the Power+Down combo never treated as a sleep hold.
- **FR-188**: ZIP reading MUST locate the end-of-central-directory record by scanning the last 1 KB (rejecting archives under 22 bytes), support stored and deflate only, skip entry names ≥256 bytes, validate that the compressed payload fits inside the archive (and that a stored entry's two size fields agree) before reading, cap whole-entry in-RAM reads at 256 KB, stream larger entries in caller-sized chunks, and fail streamed inflates that over- or under-produce and in-RAM inflates that under-produce (in-RAM inflates stop at the declared size).
- **FR-189**: Cover conversion MUST produce top-down 2-bit (four-grey) or 1-bit BMPs with Atkinson dithering from JPEG (≤2048×3072, ≥52 KB free heap, progressive at 1/8 with smoothing) and PNG (non-interlaced, legal depths, rows ≤16 KB), yielding to the scheduler periodically.
- **FR-190**: Fallible allocations MUST use nothrow forms (`makeUniqueNoThrow`, `new (std::nothrow)`, `malloc`) and be null-checked; the framebuffer storage MUST be lendable once at a time (a second lend returns null) and the lent block MUST be claimable by exactly one consumer via `buildscratch::claim()` with heap fallback; buffered file I/O MUST degrade to unbuffered passthrough on allocation failure.
- **FR-191**: The firmware MUST build one MCU family per binary (C3 combined X4/X3 with runtime detection; S3 Sticky, X4 Pro, X4 Classic, Paper Mono each their own environment) with C++20 (`-std=gnu++2a`), no exceptions, single-buffer display mode, UTF-8 long names, expat with `XML_GE=0`, a two-slot 6.25 MB OTA partition layout (plus 3.375 MB SPIFFS and 64 KB coredump partitions), and a version string of `<version>-dev-<branch>-<sha>` injected by `scripts/git_branch.py` for the `default` and `sticky` environments only (other development environments use fixed suffixes `-x4pro`, `-x4c`, `-papermono`, `-slim`; release builds `<version>`, RC builds `<version>-rc+<hash>`).
- **FR-192** *(fork-only)*: Host-reachable logic MUST be covered by the host test program (59 suites) runnable plain and under ASan/UBSan via `bin/run-tests`, with deterministic fixtures, a malformed-input corpus for parsers, and pinned "documents current limitation" tests for known defects.

#### R. Requirements added after verification

- **FR-193**: The System tab MUST offer a Wi-Fi Networks action that opens the Wi-Fi network list with auto-connect disabled so saved networks can be joined, added or forgotten, and MUST return to Settings on completion.
- **FR-194**: `GET`/`POST /api/settings` MUST also expose the KOReader Sync fields (`koUsername`, `koPassword`, `koServerUrl`, `koMatchMethod`, `koSendMetadata`, `koSyncBehavior`) grouped under "KOReader Sync", reading from and writing to `/.crosspoint/koreader.json` on each change; the password field SHOULD follow the `hasPassword`/omit-to-preserve convention of the OPDS and Wi-Fi APIs rather than being returned in clear.
- **FR-195**: In the Wi-Fi network list, Right (or Confirm when no networks were found) MUST rescan, and Left or a touch long-press on a network with a saved password MUST open the Forget Network prompt (default Cancel) and remove the credential on confirmation.
- **FR-196**: Selecting OPDS Browser on Home MUST open the catalog directly when exactly one server is stored and otherwise MUST show the server list in picker mode (servers and Add Server only), opening the chosen server's catalog and returning to Home with OPDS Browser selected on Back.
- **FR-197**: The OPDS browser MUST keep a feed navigation history: opening a folder or search result MUST push the current feed, Back MUST return to the previous feed and MUST go Home only when the history is empty; search MUST be launched by the header search icon on touch boards or by Left while the first row is selected on button boards.
- **FR-198**: Quick-return from footnotes (`pwrBtnFootnoteBack`, default On, shown in Controls only while Short Power Button Click is Footnotes, web-editable) MUST control whether a short Power click while inside a footnote returns to the origin position; when it is off a Power click inside a footnote MUST behave as an ordinary Footnotes-mode click (open the page's footnote list or single footnote).
- **FR-199**: With Short Power Button Click set to Refresh Screen, a short Power release on any non-reader screen MUST redraw the current frame with a half refresh unless the active screen performs its own forced refresh.
- **FR-200**: The Settings screen header MUST display the firmware version string in its trailing label slot.
- **FR-201**: While an EPUB chapter is being laid out System MUST show the "Indexing" popup only when the chapter HTML is at least 10 KB or an image must be fully extracted to determine its size, and MUST fire the image-triggered popup at most once per chapter build.
- **FR-202**: When the selected dictionary cannot be opened, lookup MUST show a "Dictionary error" popup, and a failed sidecar build MUST show "Not enough memory" or "Couldn't read definition" according to its cause, each auto-dismissing after 1.5 s.
- **FR-203**: A Sync Server URL without a scheme MUST be treated as `http://<url>`, trailing slashes MUST be stripped before building request paths, and the CrossPoint position extension MUST be sent only when the normalised base URL equals `https://sync.crosspointreader.com`.
- **FR-204**: A failed sync or authentication MUST show "Sync failed" with the named reason: "Network error", "Authentication failed", "Server error (try again later)", "JSON parse error", "Not enough memory for sync — please retry", "Failed to calculate document hash" or "Could not save progress".
- **FR-205**: Turning forward past the last page of a TXT, XTC or FB2 book MUST show the same end-of-book screen as EPUB ("End of book", up to three following books from the folder, Home, short Back returning to the last page).

### Key Entities *(include if feature involves data)*

- **Book (EPUB)**: a ZIP container on the SD card identified by path; owns a cache directory `epub_<hash>` holding BookMetadata (title, author, language, cover href, start reference), SpineEntry (href, cumulative size, TOC index), TocEntry (title, href, anchor, level, spine index), a CSS rule cache, per-section layout caches, inflated HTML, extracted images with pixel caches, covers/thumbnails, progress and bookmarks.
- **Section / Page / PageElement**: one laid-out spine item; pages hold text lines (TextBlock: words, x positions, styles, focus data, ruby, block style), images (ImageBlock: cache path, source, size), rules, ≤16 footnotes, ≤32 link rectangles and a visible-text offset.
- **ReaderRenderSpec**: font id, line compression, paragraph spacing, alignment, viewport, hyphenation, embedded style, image mode, focus reading; the section cache key.
- **Progress record**: spine, page, page count, optional visible-text offset per book; TXT/XTC use a page index, FB2 section/page/count.
- **BookmarkEntry**: XPath, percentage, summary, spine/page hints, visible-text offset.
- **TXT book / XTC container / FB2 book**: format-specific books with their own cache directories (`txt_`, `xtc_`, `fb2_`), page indexes, chapter records or section extents.
- **CrossPointSettings**: ~60 user preferences (display, reader, controls, system, status bar, frontlight, OPDS, keyboard, language) persisted as `settings.json`.
- **CrossPointState**: open book path, sleep-image history rings, crash-loop counter, sleep origin, splash flag (`state.json`).
- **RecentBook / RecentBooksStore**: up to ten (path, title, author, cover path) entries (`recent.json`).
- **OpdsServer / OpdsEntry**: saved catalog (name, URL, credentials) and a parsed feed row (navigation or book with title, author, href, id).
- **WifiCredential**: SSID plus obfuscated, integrity-checked password; last-connected SSID.
- **KOReader configuration / KOReaderProgress / CrossPointPosition**: sync credentials and behaviour; a wire progress record (document id, xpointer, percentage, device, optional metadata and rich position); a local position (spine, page, total, offset, paragraph, anchor).
- **Dictionary / DictionaryEntry / Sidecar**: a StarDict folder (index, data, optional synonyms and info) and its sampled-offset sidecars.
- **SD font family / SdCardFont / Manifest**: a `.cpfont` family on the card, a loaded font with per-style tables and glyph arenas, and the downloadable font catalogue (script groups, families, files with CRC).
- **Activity / ActivityResult**: a screen with lifecycle hooks and a typed result (Wi-Fi, keyboard text, menu action, chapter, percent, interval, page, progress change, network mode, footnote, file path).
- **Framebuffer / Orientation / RefreshMode**: the single 1-bpp panel buffer, the four logical orientations and Full/Half/Fast refresh.
- **Firmware image / Release asset / otadata entry**: an ESP image with chip id and board tag, a named GitHub release asset, and the boot-selection record.
- **Crash report / Log ring**: the RTC-resident panic capture and last-16-lines buffer dumped to `/crash_report.txt`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A reader can open a never-opened EPUB and reach its first page, and thereafter turn a cached page with a single partial refresh; a full-quality refresh occurs no more often than the configured cadence.
- **SC-002**: After any typography or orientation change, reopening a book lands on the page containing the same text as before the change, for every EPUB in the test corpus.
- **SC-003**: Reading position, bookmarks, settings, recent books, Wi-Fi/OPDS/KOReader configuration survive deep sleep, reboot and a power-off, and a torn write of any of these files never prevents the device from booting.
- **SC-004**: Every host-reachable parser (EPUB caches, CSS, chapter HTML, ZIP, JPEG/PNG, XTC, FB2, dictionary, dictzip, OPDS, release JSON, KOReader xpointers) rejects the malformed-input corpus (truncation, lying sizes, hostile nesting, encoding abuse) deterministically, without crashing and without an allocation larger than the input warrants, under ASan/UBSan.
- **SC-005**: The firmware runs a full reading session, a web-transfer session and a sync session on the ESP32-C3 within its 380 KB of RAM without heap exhaustion (verified on device or the official simulator with serial heap telemetry).
- **SC-006**: All 16 user stories' independent tests pass on the X4 (buttons) and on at least one touch board (X4 Pro), including all four reading orientations.
- **SC-007**: A 5 MB EPUB uploads over Wi-Fi from a browser and from Calibre and opens on the device; a WebDAV client can mount the card and perform list, upload, download, rename, move, copy and delete.
- **SC-008**: A book downloaded from an OPDS catalog appears in the configured folder with a FAT-safe name and opens.
- **SC-009**: The UI renders correctly in each of the 34 languages (no missing-string placeholders), and CJK titles render when a CJK SD font is selected.
- **SC-010**: A device running an older release detects and installs the matching per-board OTA asset and restarts on the new version; an image for another board is refused both over the air and from the SD card.
- **SC-011**: After a forced panic, the next boot writes a crash report containing the version, reset reason, panic message and last log lines, and shows the crash screen before Home.
- **SC-012**: The host test program (all suites) passes plain and under ASan/UBSan, `pio check` reports no defects at low/medium/high, and the four board builds compile in CI.

## Assumptions

- The code on fork `master` is the authoritative description of intended behaviour for this baseline; documentation drift is recorded, not specified.
- Target hardware is the ESP32-C3 Xteink X4/X3 (primary, ~380 KB RAM, no PSRAM) and the ESP32-S3 boards listed in `platformio.ini`; other FreeInk SDK boards may compile but are not verified here.
- The FreeInk SDK submodule supplies display, input, storage, battery, power, frontlight, RTC, IMU, USB-MSC, TLS and UI primitives; their internal behaviour is out of scope except where the firmware depends on it.
- The web server, hotspot, WebDAV and KOSync transport are used on trusted networks: no authentication is required by design, the hotspot is open, and TLS peer verification is disabled in the shipped wolfSSL build.
- SD-card fonts, dictionaries and firmware images are produced by the project's own tooling (`fontconvert_sdcard.py`, StarDict tools, release CI) in the documented formats.
- Fork-only capabilities (FB2, host test program, hardening fixes) are part of this baseline; upstream-only work that landed after the last sync (for example the renamed release assets) is not.
- Performance figures cited in the plan are mechanisms and measurements recorded in code comments and commit history, not new claims.
