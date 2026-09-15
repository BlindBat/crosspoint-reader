# Contract: web server, WebSocket upload, WebDAV, discovery

Active in File Transfer (Join a Network, Create Hotspot) and Calibre Wireless modes. HTTP on port 80, WebSocket on 81, UDP discovery on 8134, mDNS `crosspoint.local`. No authentication. Every response carries `Access-Control-Allow-*`; `OPTIONS` answers 204. In AP mode any unmatched non-`/api/` path redirects 302 to `/`.

## Pages

`GET /`, `/files`, `/settings`, `/fonts` (gzip HTML), `GET /js/jszip.min.js`.

## Status and files

- `GET /api/status` → `{version, ip, mode:"STA"|"AP", rssi (0 in AP), freeHeap, uptime, device, serial}`.
- `GET /api/files?path=/` → chunked array of `{name, size, isDirectory, isEpub}`; entries whose JSON exceeds 512 bytes are skipped; dot names hidden unless `showHiddenFiles`; `System Volume Information` and `XTCache` always hidden.
- `GET /download?path=` → 200 stream (`application/epub+zip` for EPUB, else octet-stream); 400 missing/invalid/directory; 403 dot or protected name; 404; 500.
- `POST /upload?path=` multipart `file` → 200 `File uploaded successfully: <name>`; 400 `File already exists: <name>` (never overwrites), create/write failures, `Upload aborted` (partial removed; a write failure returns 400 and leaves the partial file). Clears the book cache for the path.
- `POST /mkdir` (`name`, `path`) → 200 `Folder created: <name>`; 400 missing/empty/exists; 500.
- `POST /rename` (`path`, `name`) → files only; 400 invalid name / directory; 403 protected; 404; 409 `Target already exists`; cache cleared first; 200 `Renamed successfully`.
- `POST /move` (`path`, `dest`) → files only into an existing folder; 403/404/400/409; 200 `Moved successfully`.
- `POST /delete` (`path` | `paths` JSON array) → files and empty folders; per-item reasons `(cannot delete root)`, `(hidden/system file)`, `(protected file)`, `(not found)`, `(folder not empty)`, `(deletion failed)`; 200 all deleted or 500 with the list.

## Settings, fonts, OPDS, Wi-Fi

- `GET /api/settings` → chunked array `{key, name, category, type: toggle|enum|value|string, value, options[] | min,max,step}`; `POST /api/settings` JSON object → per-type validation, one save, 200 `Applied N setting(s)`; 400 missing/invalid JSON.
- `GET /api/fonts` → `{maxFamilies:128, families:[{name, sizes[], files:[{name,size}]}]}`; `POST /api/fonts/upload` multipart (`family`, one `.cpfont`) → validates family name `[A-Za-z0-9_-]`, filename, and `CPFONT\0\0` magic in the first chunk; 200 `{"ok":true}` / 400 `{"error":"Invalid .cpfont file"}`; `POST /api/fonts/delete` `{family}` → 200/400 `Invalid request`/500 `Delete failed`.
- `GET /api/opds` → `[{index, name, url, username, hasPassword}]`; `POST /api/opds` `{name,url,username,password?,index?}` → add (400 `Cannot add server (limit reached)` at 8) or update (400 `Invalid server index`; omitted `password` preserves, present-but-empty clears); `POST /api/opds/delete` `{index}`.
- `GET /api/wifi` → `[{index, ssid, hasPassword, isLastConnected}]`; `POST /api/wifi` `{ssid,password?,index?}` (400 `SSID is required`, limit 8); `POST /api/wifi/delete` `{index}`.

## WebSocket upload (`ws://<host>:81/`)

Text `START:<filename>:<size>:<path>` → `READY` | `ERROR:Upload already in progress` | `ERROR:Invalid START format` | `ERROR:File already exists: <name>` | `ERROR:Failed to create file` | `DONE` (size 0). Binary frames from the owning client append; `ERROR:No upload in progress` otherwise; `ERROR:Upload overflow` and `ERROR:Write failed - disk full?` abort and delete. `PROGRESS:<received>:<total>` every 64 KB or at completion; `DONE` when complete (cache cleared). Owner disconnect deletes the partial file. Clients send 4 KB chunks with ≤8 KB buffered.

## UDP discovery (8134)

Payload `hello` → reply `crosspoint (on <hostname>);81`.

## WebDAV (port 80, Class 1)

`OPTIONS` → `DAV: 1`, `Allow: OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK`, `MS-Author-Via: DAV`. `PROPFIND` depth 0/1 (infinity → 1) → 207 with resourcetype, getcontentlength, getcontenttype, fixed getlastmodified `Thu, 01 Jan 2024 00:00:00 GMT`. `GET`/`HEAD` files (dir → 405). `PUT` → `.davtmp` then rename; 201/204; 500 on incomplete write. `DELETE` → 204; 403 root/protected; 404; 409 non-empty. `MKCOL` → 201; 405 exists; 409 parent missing; 415 body. `MOVE`/`COPY` with `Destination` (+ `Overwrite: F` → 412; default T); COPY files only (403 dirs), MOVE also directories. `LOCK` → dummy token `urn:uuid:dummy-lock-token`; `UNLOCK` → 204. Paths are normalised (`.` dropped, `..` clamped; a path containing a backslash normalises to the root); URIs decoding to NUL → 400; dot-prefixed and protected segments → 403.

## Access point

SSID `CrossPoint-Reader`, open, channel 1, max 4 stations; DNS on 53 answers every name with the AP IP; join QR `WIFI:T:nopass;S:CrossPoint-Reader;;`; URL QR `http://crosspoint.local/`. Station hostname `CrossPoint-Reader-<12 hex MAC>`.

## HTTP client (outbound)

GET with `User-Agent: CrossPoint-ESP32-<version>`, Basic auth only when both credentials are non-empty, ≤5 redirects, 60 s timeout, final status must be 200, Wi-Fi power save disabled during transfers, destination deleted on failure or zero bytes, optional https→http downgrade of redirect targets (font downloads). The shipped wolfSSL build does not verify server certificates.
