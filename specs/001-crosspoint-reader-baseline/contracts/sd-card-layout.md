# Contract: SD-card layout

All paths are relative to the SD card root. The firmware creates `/.crosspoint` itself; users place books anywhere and assets in the named folders.

| Path | Owner | Purpose |
|------|-------|---------|
| `/.crosspoint/settings.json` | CrossPointSettings | all user preferences (flat JSON, see settings-and-input.md) |
| `/.crosspoint/state.json` | CrossPointState | open book, sleep-image rings, crash-loop counter, sleep origin, splash flag |
| `/.crosspoint/recent.json` | RecentBooksStore | `{"books":[{path,title,author,coverBmpPath}]}`, ≤10 |
| `/.crosspoint/opds.json` | OpdsServerStore | `{"servers":[{name,url,username,password_obf}]}`, ≤8 |
| `/.crosspoint/wifi.json` | WifiCredentialStore | `{lastConnectedSsid, credentials:[{ssid,password_obf,password_len,password_crc32}]}`, ≤8 |
| `/.crosspoint/koreader.json` | KOReaderCredentialStore | `{cfgVersion:2, username, password_obf, serverUrl, matchMethod, sendMetadata, syncBehavior}` |
| `/.crosspoint/sleep_frame.bin` | main | raw framebuffer for Quick Resume; deleted after restore |
| `/.crosspoint/dict.tmp`, `/.crosspoint/dicthtml.tmp` | Dictionary | last dictzip definition; normalised XHTML (removed after parse) |
| `/.crosspoint/bookmarks/<flattened book path>.json` | BookmarkFile | `{"bookmarks":[{xpath,percentage,summary,si,pc,pp,vo?}]}`; flattening strips the leading `/`, maps `/` and `\` to `_`, drops the extension |
| `/.crosspoint/epub_<hash>/` | Epub | `book.bin`, `css_rules.cache` (+`.tmp`, `.bak`), `progress.bin` (+`.tmp`), `sections/<n>.bin` (+`.part`), `html/<n>.html`, `img_<spine>_<n>.<ext>` + `.pxc`, `cover.bmp`, `cover_crop.bmp`, `thumb_<height>.bmp` |
| `/.crosspoint/fb2_<hash>/` | Fb2 (fork-only) | `book.bin`, `sections/<n>.bin`, `progress.bin`, `cover.bmp`, `thumb_<height>.bmp`, `.cover.jpg` (temporary) |
| `/.crosspoint/txt_<hash>/` | Txt | `index.bin`, `progress.bin`, `cover.bmp` |
| `/.crosspoint/xtc_<hash>/` | Xtc | `progress.bin`, `cover.bmp`, `thumb_<height>.bmp` |
| `/sleep.bmp`; `/.sleep/*.bmp`; `/sleep/*.bmp` | SleepActivity | Custom sleep images, in priority order |
| `/sleep-overlay.bmp`; `/sleep-overlay.png`; `/.sleep-overlay/*`; `/sleep-overlay/*` | SleepActivity | Transparent overlays, in priority order |
| `/.fonts/<Family>/<name>_<size>.cpfont`; `/fonts/<Family>/…` | SdCardFontRegistry | SD fonts; hidden root wins on collisions; ≤128 families; new installs go to the family's root, else `/.fonts`, else `/fonts` when only it exists |
| `/dictionaries/<folder>/`; `/.dictionaries/<folder>/` | DictionaryRegistry | one StarDict per folder (`<stem>.idx`, `.dict` or `.dict.dz`, optional `.syn`, `.ifo`; generated `.qidx`, `.sidx`) |
| `/screenshots/screenshot-<ms>.bmp`; `/screenshots/<title>/<title>_ch<n>_p<page>_<pct>pct_<ms>.bmp` | ScreenshotUtil | 1-bit BMP, rotated to portrait |
| `/read/<name>[ (N)].<ext>` | EpubReaderActivity | finished books moved here when the setting is on |
| `/crash_report.txt` | HalSystem | last crash (overwritten) |
| `/fonts_manifest.tmp` | FontDownloadActivity | downloaded `fonts.json`, deleted after parse |
| `<target>.davtmp` | WebDAVHandler | in-flight PUT, renamed over the target |

`<hash>` is `std::hash<std::string>` of the book's path, so moving or renaming a book re-keys its cache (progress and bookmarks are lost unless moved by the finish-to-`/read` flow, which renames the cache directory).

Hidden entries: the file browser and `/api/files` hide names starting with `.` unless Show Hidden Files is on; `System Volume Information` is always hidden; the web server and WebDAV additionally protect `XTCache` and refuse dot-prefixed names for download, rename, move and delete regardless of the setting.
