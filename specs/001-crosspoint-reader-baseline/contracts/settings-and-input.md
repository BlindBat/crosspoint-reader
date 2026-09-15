# Contract: settings.json keys, input vocabulary and timings

## settings.json

Flat JSON object at `/.crosspoint/settings.json`; enums are stored as option indices, toggles as 0/1. Keys marked * exist only on boards where the entry is present (touch, home key, IMU, frontlight, RTC).

| Key | Type / range | Default |
|-----|--------------|---------|
| sleepScreen | 0 Dark, 1 Light, 2 Custom, 3 Cover, 4 Cover+Custom, 5 None, 6 Quick Resume, 7 Transparent | 0 |
| sleepScreenCoverMode | 0 Fit, 1 Crop | 0 |
| sleepScreenCoverFilter | 0 None, 1 Contrast, 2 Inverted | 0 |
| quickResumeSleepScreen | 0 Off, 1 On | 0 |
| hideBatteryPercentage | 0 Never, 1 In Reader, 2 Always | 0 |
| refreshFrequency | 0 =1, 1 =5, 2 =10, 3 =15, 4 =30 pages, 5 Never | 3 |
| uiTheme | 0 Classic, 1 Lyra, 2 Lyra Extended, 3 RoundedRaff | 1 |
| fadingFix | toggle (hidden on X4 Pro/X4 Classic) | 0 |
| frontlightRestoreOnWake* | toggle | 1 |
| screenInverted | toggle (Night Mode) | 0 |
| fontFamily | 0 Noto Serif, 1 Noto Sans (legacy 2 = OpenDyslexic → SD family) | 0 |
| fontSize | point size (12/14/16/18 built-in or SD sizes; legacy slots 0..3 folded) | 14 |
| sdFontFamilyName | string ≤31 (omitted when empty) | "" |
| lineSpacing | 0 Tight, 1 Normal, 2 Wide, 3 Extra Wide | 1 |
| screenMargin | 5..40 step 5 | 5 |
| paragraphAlignment | 0 Justified, 1 Left, 2 Center, 3 Right, 4 Book's Style | 0 |
| embeddedStyle, hyphenationEnabled, extraParagraphSpacing, textAntiAliasing, focusReadingEnabled | toggles | 1, 0, 1, 1, 0 |
| orientation | 0 Portrait, 1 Landscape CW, 2 Portrait 180°, 3 Landscape CCW | 0 |
| imageRendering | 0 Display, 1 Placeholder, 2 Suppress | 0 |
| readerMenuStyle* | 0 List, 1 Toolbar (touch boards seed 1) | 0 |
| sideButtonLayout | 0 Prev/Next, 1 Next/Prev, 2 Disabled | 0 |
| touchReaderControls* | 0 Off, 1 Tap, 2 Swipe, 3 Inverted Tap | 2 |
| tapForReaderMenu* | 0 Off, 1 Tap, 2 Swipe Up | 1 |
| frontButtonFollowOrientation* | toggle | 0 |
| frontButtonBack/Confirm/Left/Right | hardware index 0..3 (permutation; duplicates reset to identity) | 0,1,2,3 |
| longPressButtonBehavior | 0 Off, 1 Chapter skip, 2 Orientation change | 0 |
| longPressMenuFunction | 0 KOSync, 1 Disabled, 2 Bookmark, 3 Dictionary, 4 Reader Menu (home-key boards) | 1 |
| shortPwrBtn | 0 Ignore, 1 Sleep, 2 Page Turn, 3 Refresh Screen, 4 Footnotes, 5 Confirm (touch builds) | 0 |
| tiltPageTurn* | 0 Off, 1 Normal, 2 Inverted | 0 |
| pwrBtnFootnoteBack | toggle (documented intent per FR-198; not read by the shipped code) | 1 |
| backShortToFileBrowser* | toggle | 0 |
| sleepTimeoutMinutes | 1..31 (31 = Never; legacy `sleepTimeout` enum migrated to 1/5/10/15/30) | 10 |
| showHiddenFiles, removeReadBooksFromRecents, moveFinishedToReadFolder | toggles | 0 |
| opdsDownloadFolder | string ≤63, "" = root, else `/path` | "" |
| opdsFilenameFormat | 0 Author - Title, 1 Title - Author, 2 Title | 0 |
| frontlightBrightness, frontlightWarmth*, frontlightOn | 0..100, 0..100, toggle | 60, 50, 0 |
| statusBarChapterPageCount, statusBarBookProgressPercentage, statusBarBattery | toggles | 1 |
| statusBarProgressBar | 0 Book, 1 Chapter, 2 Hide | 2 |
| statusBarProgressBarThickness | 0 Thin, 1 Medium, 2 Thick (height = (t+1)×2 px) | 1 |
| statusBarTitle | 0 Book, 1 Chapter, 2 Hide | 1 |
| xtcStatusBarMode | 0 Hide, 1 Bottom, 2 Top | 0 |
| statusBarClock*, clockFormat*, clockUtcOffsetQ*, clockHasBeenSynced* | 0 Hide/1 Right/2 Left; 0 24h/1 12h; quarter-hours + 48 (0..104); toggle | 0, 0, 48, 0 |
| dictionaryName | folder name ≤31 (omitted when empty) | "" |
| language | language code string (e.g. "DE"); unknown → EN | "EN" |
| keyboardLayouts | u16 bitmask by table position (EN, FR, DE, ES, RU, UK, BE, KK, HE); omitted while 0 | 0 |
| koUsername, koPassword, koServerUrl, koMatchMethod, koSendMetadata, koSyncBehavior | web-API aliases into koreader.json | — |

Load rules: enum → clamp to option count; toggle → 0/1; value → [min, max]; wrong type or out of range keeps the in-memory value; missing key keeps the current value except fontFamily/fontSize/front buttons (defaults) and sdFontFamilyName/dictionaryName (cleared); strings truncated to capacity−1; `<key>_obf` read with plaintext fallback. A migration resave rewrites the whole document.

## Logical buttons and gestures

| Logical | Physical |
|---------|----------|
| Back, Confirm, Left, Right | front buttons via the remap table |
| Up, Down, Power | fixed (never remapped) |
| PageBack / PageForward | side buttons per `sideButtonLayout`; flipped when the navigation axis is swapped |
| NavNext / NavPrevious | Down or Right / Up or Left (flipped likewise) |
| ScreenLeft/Right/Up/Down | rotate by the live renderer orientation when `frontButtonFollowOrientation` |
| Axis swap | (touch board or follow-orientation) and live orientation ∈ {Portrait 180°, Landscape CCW} |
| Touch | left-edge right swipe = Back; bottom-edge up swipe or Home-key tap = Home; top-edge down swipe = menu gesture or light panel (frontlight boards); Home-key hold; tap in the top 44 px of Home/Browse/Settings/File Transfer = control center |

## Timings

| Event | Value |
|-------|-------|
| Hold-to-sleep | 400 ms (10 ms when shortPwrBtn = Sleep); not within 2 s of boot |
| Wake verification | 10 ms stable sample |
| X4 Pro double-click | two releases ≤300 ms held within 500 ms |
| Page-button long press (chapter skip / orientation) | 700 ms |
| Bookmark / Dictionary long press | 400 ms |
| KOSync long press | 1000 ms (requires credentials) |
| Back long press (file browser) | 1000 ms |
| Delete / remove-recent long press | 1000 ms |
| List paging | hold > 500 ms, repeat every 500 ms |
| Page-turn debounce | 200 ms, one pending turn |
| Idle glyph prewarm | 400 ms |
| CPU low-power | after 3 s idle (10 MHz; 80 MHz with PSRAM) |
| Tilt | > 270°/s trigger, < 50°/s re-arm, 600 ms cooldown, 300 ms settle, 20 Hz poll |
| Keyboard long press | 500 ms / 1500 ms clear (GPIO); 350 ms / 900 ms (touch) |
| Auto-return after sync | 1200 ms |
| Popups | bookmark 2.5 s, dictionary errors 1.5 s, screenshot border 1 s |

## Serial protocol (debug builds)

Host → device: `CMD:SCREENSHOT\n`. Device → host: `SCREENSHOT_START:<bytes>\n`, raw framebuffer, `SCREENSHOT_END\n`. Every 10 s while serial is connected: `[MEM] Free: N bytes, Total: N bytes, Min Free: N bytes, MaxAlloc: N bytes`. Log lines: `[ms] [ERR|INF|DBG] [ORIGIN] message`.
