# Webserver Endpoints

This document describes the HTTP, WebSocket, WebDAV, and discovery endpoints
available while CrossPoint Reader is in File Transfer or Calibre Wireless mode.

- HTTP server: port 80
- WebSocket upload server: port 81
- UDP discovery listener: port 8134
- WebDAV: port 80, handled by the same HTTP server
- Captive-portal DNS, hotspot mode only: UDP port 53

Examples use `crosspoint.local`. If mDNS does not resolve on your network, use
the IP address shown on the device screen.

There is no authentication. Every response carries `Access-Control-Allow-*`
headers.

Routes are matched in registration order, and the WebDAV handler is registered
last but claims every `OPTIONS`, `GET`, `HEAD`, `PUT`, `DELETE`, `PROPFIND`,
`MKCOL`, `MOVE`, `COPY`, `LOCK` and `UNLOCK` request that none of the pages or
API routes below matched. An unknown `GET` path is therefore answered by WebDAV —
`404 Not Found` when the path is not on the card — and an `OPTIONS` request gets
the WebDAV `200` with `DAV: 1` and `Allow:` headers. The built-in not-found
handler, which in hotspot (AP) mode redirects unmatched non-`/api/` paths to `/`
with a `302` for captive-portal detection, only sees methods the WebDAV handler
does not claim.

## HTTP Pages

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | Home/status page |
| `GET` | `/files` | File manager page |
| `GET` | `/settings` | Web settings page |
| `GET` | `/fonts` | SD-card font manager page |
| `GET` | `/js/jszip.min.js` | JavaScript asset used by the file manager |

## Device Status

### `GET /api/status`

```bash
curl http://crosspoint.local/api/status
```

Response:

```json
{
  "version": "1.6.0",
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600,
  "device": "X4",
  "serial": "Not found"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `version` | string | Firmware version |
| `ip` | string | Device IP address |
| `mode` | string | `"STA"` for joined Wi-Fi or `"AP"` for hotspot mode |
| `rssi` | number | Wi-Fi RSSI in dBm; `0` in AP mode |
| `freeHeap` | number | Free heap in bytes |
| `uptime` | number | Seconds since boot |
| `device` | string | On X4/X3 builds, `"X3"` or `"X4"` from runtime hardware detection. On other builds, the board profile name, for example `sticky`, `xteink_x4_pro`, `xteink_x4_classic` or `m5stack_paper_mono`. |
| `serial` | string | Serial number read from the eFuse user-data block, or `"Not found"` when the block is blank or not printable |

## File Management

### `GET /api/files`

Lists files and folders under a directory.

```bash
curl "http://crosspoint.local/api/files?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Directory to list |

Response:

```json
[
  {"name":"MyBook.epub","size":1234567,"isDirectory":false,"isEpub":true},
  {"name":"Notes","size":0,"isDirectory":true,"isEpub":false}
]
```

Hidden dotfiles are omitted from this listing unless the device setting
`showHiddenFiles` is enabled. `System Volume Information` and `XTCache` are
always hidden/protected.

The setting affects **only** this listing; it never relaxes the checks below.

`/api/files`, `/download`, `/delete`, `/upload` and `/mkdir` normalise the path
they are given and then refuse it if **any** component is dot-prefixed or
protected, not just the last one — so a normally-named file inside a hidden
folder is not reachable by its full path either. `/upload` and `/mkdir`
additionally screen the name they are given, rejecting an empty or
whitespace-only name and a name containing `/` or `\`.

`/rename` and `/move` test the item's own name and the new name, not the whole
path. WebDAV refuses a protected segment anywhere in the path (`PROPFIND` and
`LOCK`/`UNLOCK` excepted — see below).

The response is streamed as chunked JSON. An entry whose serialized JSON does
not fit the handler's 512-byte entry buffer is skipped rather than truncated.

### `GET /download`

Downloads a file from the SD card.

```bash
curl -OJ "http://crosspoint.local/download?path=/Books/MyBook.epub"
```

Query parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | File path to download |

A path with a dot-prefixed, `System Volume Information` or `XTCache` component
anywhere in it is refused with `403`, whatever `showHiddenFiles` is set to. A
missing file returns `404`, and a directory path returns `400`. EPUB files are served as `application/epub+zip`; other files use
`application/octet-stream`.

### `POST /upload`

Uploads a file with HTTP multipart form data.

```bash
curl -X POST -F "file=@mybook.epub" "http://crosspoint.local/upload?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Destination directory |

Successful response:

```text
File uploaded successfully: mybook.epub
```

Notes:

- Uploads never overwrite. If the destination already exists the request fails
  with `400` and the body `File already exists: <name>`. The browser file manager
  avoids this by reading `/api/files` first and suffixing a colliding name —
  `MyBook.epub` becomes `MyBook (2).epub`, then `MyBook (3).epub` — comparing
  names case-insensitively.
- The book cache for the uploaded path is cleared after a successful upload.
  Only EPUB, FB2, XTC and TXT paths have a cache; any other extension is a no-op.
- HTTP upload uses a 4 KB write buffer before flushing to the SD card.
- An aborted upload closes and deletes the partial file. A write failure (for
  example a full card) closes the file and returns `400`, but leaves the partial
  file on the card.

### `POST /mkdir`

Creates a folder.

```bash
curl -X POST -d "name=NewFolder&path=/" http://crosspoint.local/mkdir
```

Form parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `name` | Yes | - | New folder name |
| `path` | No | `/` | Parent folder |

Returns `200 Folder created: <name>`. A missing or empty name returns `400`, an
existing path returns `400 Folder already exists`, and a failed `mkdir` returns
`500`.

### `POST /rename`

Renames a file.

```bash
curl -X POST -d "path=/Books/old.epub&name=new.epub" http://crosspoint.local/rename
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `name` | Yes | New file name, not a path |

Only files can be renamed through this endpoint. `path` is normalised first
(`.` segments dropped, `..` clamped at the root, a path containing a backslash
collapsing to `/`). A protected source or target name returns `403`, a missing
item `404`, and an existing target `409 Target already exists`. A new name that
contains `/` or `\` returns `400`, and a new name equal to the old one returns
`200 Name unchanged`. The old path's book cache is cleared before the rename.

### `POST /move`

Moves a file into an existing folder.

```bash
curl -X POST -d "path=/Books/mybook.epub&dest=/Read" http://crosspoint.local/move
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `dest` | Yes | Existing destination folder |

Only files can be moved through this endpoint, and `dest` must be an existing
folder. Both parameters are normalised as for `/rename`. A protected source or
destination returns `403`, a missing item or destination `404`, a destination
that is not a folder `400`, and an existing target `409 Target already exists`.
The old path's book cache is cleared before the move.

### `POST /delete`

Deletes one or more files or empty folders.

```bash
curl -X POST -d "path=/Books/mybook.epub" http://crosspoint.local/delete
curl -X POST -d 'paths=["/Books/old.epub","/OldFolder"]' http://crosspoint.local/delete
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes, unless `paths` is provided | Single path to delete |
| `paths` | Yes, unless `path` is provided | JSON array of paths to delete |

Supply either `path` or `paths`, not both. Each path is normalised and then
refused if any component of it is protected, so nothing inside a hidden folder
can be deleted either. Non-empty folders are rejected. The book cache for each
deleted file is cleared.

Deletion is per item: the response is `200 All items deleted successfully`, or
`500` listing each failure with its reason — `(cannot delete root)`,
`(protected file)`, `(not found)`, `(folder not empty)` or `(deletion failed)`.

## Settings API

### `GET /api/settings`

Returns a streamed JSON array of editable settings. Each item contains common
fields plus type-specific fields.

```bash
curl http://crosspoint.local/api/settings
```

Example item:

```json
{
  "key": "fontSize",
  "name": "Reader Font Size",
  "category": "Reader",
  "type": "enum",
  "value": 1,
  "options": ["12 pt", "14 pt", "16 pt", "18 pt"]
}
```

`value` is always an index into `options`, never the option's text. `fontSize`
is one of the settings whose `options` are built at request time — they are the
point sizes the selected font family actually ships, so a family installed at
10/12/14 offers three options. `fontFamily` varies the same way, from the SD
card contents. The dictionary picker is device-only: it carries no `key`, and
the web handler passes no dictionary list, so it never appears here.

Which entries are listed also depends on the board. Settings that do not apply
to the running hardware are dropped before the list is serialized: **Touch
Reader Controls** and **Reader Menu Style** on a buttons-only board; **Orient
front buttons**, **Sunlight Fading Fix** and **Short Back to File Browser** on a
touch board; **Show Reader Menu** on any board without a capacitive Home key.

Types:

| Type | Extra fields |
|------|--------------|
| `toggle` | `value` (`0` or `1`) |
| `enum` | `value`, `options` |
| `value` | `value`, `min`, `max`, `step` |
| `string` | `value` |

The font-family setting includes SD-card font families when they are installed.

### `POST /api/settings`

Applies a partial settings update from a JSON object.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"fontSize":2,"showHiddenFiles":1}' \
  http://crosspoint.local/api/settings
```

Successful response:

```text
Applied 2 setting(s)
```

Unknown keys and out-of-range values are ignored and not counted. A missing body
or invalid JSON returns `400`. All settings are persisted once, after the whole
payload has been applied.

## Font Management API

### `GET /api/fonts`

Lists installed SD-card font families.

```bash
curl http://crosspoint.local/api/fonts
```

Response:

```json
{
  "maxFamilies": 128,
  "families": [
    {
      "name": "Literata",
      "sizes": [12, 14, 16, 18],
      "files": [
        {"name": "Literata_12.cpfont", "size": 123456}
      ]
    }
  ]
}
```

### `POST /api/fonts/upload`

Uploads one `.cpfont` file into a family folder.

```bash
curl -X POST \
  -F "family=Literata" \
  -F "file=@Literata_12.cpfont" \
  http://crosspoint.local/api/fonts/upload
```

The handler validates the family name (alphanumerics, `-` and `_` only), the
`.cpfont` filename (spaces are first replaced with `_`; the basename must be
alphanumerics, `-` and `_`, with no extra dots) and the `CPFONT` magic bytes in
the first chunk. A file that fails any check is deleted rather than kept.

Successful response:

```json
{"ok":true}
```

Rejected upload:

```json
{"error":"Invalid .cpfont file"}
```

Families are written under `/.fonts/` or `/fonts/`, reusing the root the family
already lives in; a new family goes to `/.fonts/` unless only `/fonts/` exists.
After an upload or delete, the font registry is marked dirty and rebuilt on the
next `GET /api/fonts` or the next time the on-device settings list is built.

### `POST /api/fonts/delete`

Deletes an installed font family.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"family":"Literata"}' \
  http://crosspoint.local/api/fonts/delete
```

Successful response:

```json
{"ok":true}
```

A malformed body returns `400 {"error":"Invalid request"}`; a failed delete
returns `500 {"error":"Delete failed"}`.

## OPDS Server API

### `GET /api/opds`

Lists saved OPDS servers. Passwords are never returned.

```bash
curl http://crosspoint.local/api/opds
```

Response:

```json
[
  {
    "index": 0,
    "name": "My Catalog",
    "url": "http://calibre.local:8080/opds",
    "username": "reader",
    "hasPassword": true
  }
]
```

### `POST /api/opds`

Adds or updates an OPDS server. Include `index` to update an existing entry.
If `password` is omitted during an update, the existing password is preserved; a
present-but-empty `password` clears it. Up to eight servers can be stored; an add
beyond the limit returns `400 Cannot add server (limit reached)`, and an
out-of-range `index` returns `400 Invalid server index`.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"name":"My Catalog","url":"http://calibre.local:8080/opds","username":"reader","password":"secret"}' \
  http://crosspoint.local/api/opds
```

### `POST /api/opds/delete`

Deletes an OPDS server by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/opds/delete
```

## Wi-Fi Credential API

### `GET /api/wifi`

Lists saved Wi-Fi networks. Passwords are never returned.

```bash
curl http://crosspoint.local/api/wifi
```

Response:

```json
[
  {
    "index": 0,
    "ssid": "HomeWiFi",
    "hasPassword": true,
    "isLastConnected": true
  }
]
```

### `POST /api/wifi`

Adds or updates a saved Wi-Fi network. Include `index` to update an existing
entry. If `password` is omitted during an update, the existing password is
preserved; an empty password is valid for open networks. A missing `ssid`
returns `400 SSID is required`. Posting an `ssid` that is already saved updates
that entry's password instead of adding a second one. Up to eight networks can
be stored; an add beyond the limit returns `400 Cannot add network (limit
reached)`, and an out-of-range `index` returns `400 Invalid network index`.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"ssid":"HomeWiFi","password":"secret"}' \
  http://crosspoint.local/api/wifi
```

### `POST /api/wifi/delete`

Deletes a saved Wi-Fi network by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/wifi/delete
```

## WebSocket Upload

### Port 81

The WebSocket path is used for fast binary uploads from the file manager and
Calibre plugin workflows.

Connection:

```text
ws://crosspoint.local:81/
```

Protocol:

1. Client sends text: `START:<filename>:<size>:<path>`
2. Server replies `READY`
3. Client sends binary chunks
4. Server sends `PROGRESS:<received>:<total>` every 64 KB or at completion
5. Server sends `DONE` when complete or `ERROR:<message>` on failure

`<filename>` is everything up to the first `:` after the prefix and `<path>` is
everything after the second, so a path may itself contain `:`. The size token
accepts an optional `+` and decimal digits only.

Example session:

```text
Client -> START:mybook.epub:1234567:/Books
Server -> READY
Client -> [binary chunk]
Server -> PROGRESS:65536:1234567
...
Server -> DONE
```

A declared size of `0` is a special case: the server creates and closes the empty
file, clears the book cache and replies `DONE` immediately. There is no `READY`
and no binary frame in that exchange.

Error messages include:

| Message | Cause |
|---------|-------|
| `ERROR:Upload already in progress` | A second upload was started before the first completed |
| `ERROR:Invalid START format` | Malformed START message or invalid size token |
| `ERROR:File already exists: <name>` | Destination already exists — WebSocket uploads never overwrite either |
| `ERROR:Failed to create file` | Destination file could not be opened |
| `ERROR:No upload in progress` | Binary data arrived without a matching START, or from a client that does not own the active upload |
| `ERROR:Upload overflow` | Client sent more bytes than declared |
| `ERROR:Write failed - disk full?` | SD write failed |

Only one upload runs at a time, and only the client that sent `START` may send
binary frames. Incomplete WebSocket uploads are deleted when that client
disconnects, on overflow, or on a write error. The book cache for the uploaded
path is cleared on completion. The browser file manager sends 4 KB chunks and
waits whenever more than 8 KB is still buffered on the socket.

## WebDAV

The same HTTP server registers a WebDAV Class 1 handler for file manager clients.
`OPTIONS` advertises `DAV: 1` and `MS-Author-Via: DAV`.

Supported methods:

```text
OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK
```

| Method | Behaviour |
|--------|-----------|
| `PROPFIND` | `207` multistatus. `Depth: 0` lists the resource itself; `1`, a missing header or `infinity` list one level. |
| `GET` / `HEAD` | `GET` serves files only; a directory returns `405`. `HEAD` answers `200` for a directory. |
| `PUT` | Writes `<path>.davtmp`, then renames it into place. `201` for a new file, `204` for a replacement, `500` when the write failed, the parent folder is missing, or the target is an existing directory. |
| `DELETE` | Files and empty folders. `403` for the root, `404` when missing, `409` for a non-empty folder, `204` on success. |
| `MKCOL` | `201` on success, `405` if the path exists, `409` if the parent is missing, `415` if a body is sent. |
| `MOVE` | Files and folders. `403` for the root, `409` when the destination's parent is missing, `412` when the destination exists and `Overwrite: F` was sent; `Overwrite` defaults to true. |
| `COPY` | Files only — a directory source returns `403`. Same `Overwrite` rules as `MOVE`. |
| `LOCK` / `UNLOCK` | Accepted for client compatibility only. |

Notes:

- Unlike the HTTP and WebSocket upload endpoints, `PUT`, `MOVE` and `COPY` do
  replace an existing destination. `PUT` always replaces; `MOVE` and `COPY`
  honour the `Overwrite` header and refuse with `412` only when it is `F`.
- Paths are normalised before use: `.` segments are dropped, `..` is clamped at
  the root, and a path containing a backslash collapses to `/` rather than
  returning an error.
- A request URI or `Destination` header that would decode to an embedded NUL
  (`%00`, and malformed escapes that decode the same way) is rejected: the URI
  returns `400 Bad Request`, and such a `Destination` is treated as missing.
- `GET`, `HEAD`, `PUT`, `DELETE`, `MKCOL`, `MOVE` and `COPY` refuse a
  dot-prefixed segment, `System Volume Information` or `XTCache` **anywhere** in
  the path with `403`, so `/.crosspoint/settings.json` is unreachable even
  though only its first segment is hidden. `PROPFIND` does not apply that check
  to the requested path itself — it only omits protected entries from the
  children it lists — and `LOCK`/`UNLOCK` do not check paths at all.
- Modification times are a fixed `Thu, 01 Jan 2024 00:00:00 GMT`; the device has
  no reliable wall clock for file timestamps.
- `LOCK` returns the same dummy token `urn:uuid:dummy-lock-token` every time. The
  server does not implement full WebDAV Class 2 locking semantics such as
  persistent locks or lock discovery.
- The book cache is cleared on `PUT`, `DELETE` of a file, and `MOVE`. `COPY`
  does not clear it.

## UDP Discovery

The server listens on UDP port `8134`. When it receives the text payload
`hello`, it replies to the sender with:

```text
crosspoint (on <hostname>);81
```

The final field is the WebSocket upload port.

## Network Modes

### Station Mode (STA)

- Device joins an existing 2.4 GHz Wi-Fi network.
- `crosspoint.local` is advertised with mDNS when available.
- `/api/status` returns `"mode": "STA"` and RSSI in dBm.

### Access Point Mode (AP)

- Device creates an open hotspot named `CrossPoint-Reader`.
- The device shows a Wi-Fi QR code and URL QR code.
- Beneath the URL QR code the reader prints its own AP address as a fallback
  (normally `192.168.4.1`).
- `/api/status` returns `"mode": "AP"` and `"rssi": 0`.

### Calibre Wireless

Calibre Wireless starts the same web server in STA mode and displays setup
instructions plus WebSocket upload progress on the device screen.

### USB Drive

USB Drive, offered under File Transfer on boards built with USB mass-storage
support (`x4pro`, `x4c`, `papermono`), hands the raw SD card to the host over
USB. No Wi-Fi is started and none of the endpoints above are available in that
mode.
