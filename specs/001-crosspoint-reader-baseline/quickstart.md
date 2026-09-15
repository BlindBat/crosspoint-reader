# Quickstart: building, testing and verifying the baseline

## Prerequisites

- pioarduino PlatformIO Core (pinned 6.1.19 in CI) or the pioarduino VS Code extension; Python 3.8+; clang-format 21; CMake + Ninja for the host tests (PlatformIO's bundled `tool-cmake`/`tool-ninja` are found automatically by `bin/run-tests`).
- Clone with submodules: `git clone --recursive …` or `git submodule update --init --recursive`.
- Per-developer settings (serial port, simulator envs) go in gitignored `platformio.local.ini`.

## Quality gates (run in this order before merging into fork `master`)

```bash
./bin/clang-format-fix -c            # 1. formatting gate (never call clang-format directly)
bin/run-tests                        # 2. host test program, plain (43 suites)
bin/run-tests --asan                 # 3. same under ASan+UBSan (separate build dir)
pio run -e default                   # 4. ESP32-C3 firmware builds (X4 + X3)
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high   # 5. cppcheck
```

Useful variants: `bin/run-tests --quick` (skips the two slowest suites; what the opt-in pre-push hook runs), `bin/run-tests --filter css` (one suite), `bin/run-tests --full` (adds `pio check`), `pio run -t unit-tests` (same tests from PlatformIO), `bin/install-hooks` (opt-in pre-commit format + pre-push gate).

Other boards: `pio run -e sticky`, `-e x4pro`, `-e x4c`, `-e papermono`; release variants `*-gh_release`; `slim` strips serial logging.

## Flash and monitor

```bash
pio run -t upload                                   # default env, USB-C
python3 scripts/debugging_monitor.py                # colour log monitor, live heap graph, CMD: prompt
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101   # macOS explicit port
```

Or use the web flasher at https://crosspointreader.com/#flash-tools with the built `.pio/build/<env>/firmware.bin`. Command line: `esptool.py --chip esp32c3 --port <port> --baud 921600 write_flash 0x10000 firmware.bin` (S3 boards: `--chip esp32s3`).

## Regenerating generated sources

- Web pages: edit `src/network/html/*.html`; `scripts/build_html.py` runs at build time.
- Translations: edit `lib/I18n/translations/<language>.yaml`; `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` (the build strips unused keys).
- Hyphenation tries: `scripts/update_hyphenation.sh` → `lib/Epub/Epub/hyphenation/generated/`.
- Test fixtures: `scripts/generate_test_{corpus,zips,images,xtc,dict,cpfonts}.py`, `scripts/generate_fb2_test_fixtures.py`, `scripts/generate_font_decompressor_golden.py` (deterministic; `.cpfont` fixtures are generated at test-build time).

## On-device verification checklist (one line per user story)

1. **US1** Open a fresh EPUB (Indexing popup), turn 10 pages, change font size, confirm the same text is shown; sleep and wake back into the book.
2. **US2** Browse nested folders, open a book, return Home: Continue Reading tile and Recent Books updated; hold Confirm on a file to delete it.
3. **US3** Set Time to Sleep = 1 min, wait, confirm the sleep screen; press Power: no splash; try each sleep screen mode including Quick Resume and Transparent.
4. **US4** Reader menu → Select Chapter, Footnotes, Go to %, Toggle Bookmark, Bookmarks → open, Cancel/Delete.
5. **US5** Text Settings tabs with preview; four orientations; Night Mode; Customise Status Bar preview; each UI theme.
6. **US6** Open a `.txt`, an `.xtc`/`.xtch` (chapter list, XTC status bar) and an `.fb2`; sleep with Cover mode for each.
7. **US7** Manage Fonts → download a CJK family; select it; open a CJK book; confirm CJK file names in the browser; open a Hebrew/Arabic book.
8. **US8** File Transfer → Join a Network → upload from `/files`, mount WebDAV, send from Calibre; Create Hotspot; on X4 Pro use USB Drive.
9. **US9** Add an OPDS server, browse, search, download; check the file name and folder.
10. **US10** Remap Front Buttons; each Short Power Button Click mode; tilt on an IMU board; control center on a frontlight board.
11. **US11** Switch language; enable a second keyboard layout and cycle with the language key.
12. **US12** Select a dictionary; Look Up a word; check Not found / Indexing popups; HTML dictionary paging.
13. **US13** Authenticate, Sync Progress in Smart and Ask modes against `sync.crosspointreader.com`.
14. **US14** Check for updates on an older release; SD Card Firmware Update with a valid and an invalid `.bin`; recovery boot (Up/Down + Power).
15. **US15** Auto Turn at 3 ppm; Power+Side Down screenshot; Show page as QR.
16. **US16** Force a panic in a debug build; check `/crash_report.txt` and the crash screen; send `CMD:SCREENSHOT` from the monitor.

Heap: keep `[MEM] Free` above ~50 KB during reading and watch `MaxAlloc` before Wi-Fi/TLS activities.

## Cache hygiene

Delete `/.crosspoint/` (or only `epub_*` directories) after changing `lib/Epub/Epub/Section.cpp`, `BookMetadataCache.cpp` or any render setting semantics; bump `SECTION_FILE_VERSION` / `BOOK_CACHE_VERSION` before changing a binary layout and document it in `docs/file-formats.md`.
