# Web Server Guide

This guide explains how to use CrossPoint Reader's built-in web server for file
transfer, device settings, Wi-Fi/OPDS management, and SD-card font management.

## Overview

The web server is available in the **Join a Network**, **Calibre Wireless**
and **Create Hotspot** modes under **File Transfer**. It can:

- Upload, download, rename, move, and delete files on the SD card
- Create folders
- Serve the same tree to WebDAV clients
- Edit many device settings from a browser
- Manage saved Wi-Fi networks and OPDS servers
- Upload and delete `.cpfont` SD-card font families
- Accept Calibre wireless uploads

Names that start with a dot, plus `System Volume Information` and `XTCache`, are
treated as protected. The **Show Hidden Files** setting only controls whether
dot-prefixed entries appear in the browser's folder listing; downloading,
renaming, moving and deleting such an item is refused regardless of the setting.
WebDAV goes further: fetching, writing or deleting a file whose path contains a
protected segment anywhere is refused, so a normally-named file inside a hidden
folder is unreachable there.

The server does not require authentication. Use it only on trusted private
networks or in hotspot mode when you control who is connected.

## Starting File Transfer

1. From the Home screen, select **File Transfer**.
2. Choose one of the available modes:

| Mode | Use when |
|------|----------|
| **Join a Network** | You want the reader to join an existing Wi-Fi network. |
| **Calibre Wireless** | You want to receive books from the CrossPoint Calibre plugin workflow. |
| **Create Hotspot** | You want the reader to create its own open Wi-Fi network. |
| **USB Drive** | You want to manage the SD card over USB. Only shown on boards built with USB mass-storage support (`x4pro`, `x4c`, `papermono`); the `default` (X4/X3) and `sticky` builds do not offer it. |

## Join a Network Mode

1. Select **Join a Network**.
2. If you have saved Wi-Fi credentials, CrossPoint first tries the last
   connected network, then other visible saved networks in signal-strength
   order. Press **Back** to cancel or **Confirm** to stop auto-connect and show
   the network list.
3. If the network list is shown, pick a 2.4 GHz Wi-Fi network from the scan
   results.
4. Enter the password if prompted.
5. Save credentials if you want the reader to reconnect automatically next time.

After connection, the reader shows:

- The connected SSID
- A QR code encoding the IP URL
- The same IP URL as text, for example `http://192.168.1.102/`
- The mDNS fallback URL, `http://crosspoint.local/`

Use either URL from a phone, tablet, or computer on the same network.

## Create Hotspot Mode

1. Select **Create Hotspot**.
2. Connect your phone or computer to the open Wi-Fi network:

```text
CrossPoint-Reader
```

3. Open the URL shown on the reader. `http://crosspoint.local/` is shown first
   and is encoded in the QR code; the reader prints its own AP address beneath
   it as a fallback (normally `http://192.168.4.1/`).

The hotspot is open (no password), runs on channel 1, and accepts up to four
clients. The reader displays one QR code for joining the hotspot and another QR
code for opening the web interface.

## Calibre Wireless Mode

Calibre Wireless starts the same web server in station mode, then displays setup
instructions and upload progress on the reader. Use this mode with the
CrossPoint Calibre plugin or other clients that speak the documented WebSocket
upload protocol.

For Calibre OPDS browsing, add `/opds` to the catalog URL when configuring an
OPDS server.

## USB Drive Mode

USB Drive is listed under File Transfer only on boards built with USB
mass-storage support (`x4pro`, `x4c`, `papermono`). It does **not** start the web
server: it hands the raw SD card to the computer over USB, so the card appears as
a removable drive.

1. Select **USB Drive**.
2. The reader shows "Preparing USB Drive...", then "Connect this reader to your
   computer", then "USB Drive Connected" once the host attaches. Connecting can
   take up to 30 seconds.
3. Copy files with your computer's file manager, then eject the drive or unplug
   the cable.

Notes:

- The reader returns Home when the host ejects or disconnects the drive.
- While it is waiting for a host, **Back**, **Power** or the Home gesture exit.
  The wait times out on its own after five minutes.
- If the drive cannot be started, the failure message clears after 30 seconds and
  the reader returns Home.
- Auto-sleep is suppressed while the drive is connected.

## Web Interface

The browser UI has four primary pages.

### Home

The Home page shows the device serial number, firmware version, IP address, and
free memory, plus a Wi-Fi status row that is fixed text and always reads
"Connected". The full status payload — including network mode, RSSI, uptime and
the device model — is available from `/api/status`.

### File Manager

The File Manager page can:

- Browse SD-card folders
- Upload files, using WebSocket upload when available and HTTP upload as a fallback
- Create folders
- Download files
- Rename files
- Move files into existing folders
- Delete one or more selected files or empty folders

Uploads never overwrite. Before an upload starts, the browser reads the current
folder listing and gives a colliding file a numbered suffix — `MyBook.epub`
becomes `MyBook (2).epub`, then `MyBook (3).epub`, and so on (the comparison
ignores case). If a name still collides on the device, the upload is refused with
`File already exists`. When a book file is moved, renamed, or deleted through the
web server, its cache is cleared so stale metadata is not reused; it is also
cleared for each newly uploaded path. Only EPUB, FB2, XTC and TXT paths have a
cache to clear.

The File Manager also offers multi-select for delete, optional renaming of EPUBs
from their OPF metadata, and an optional client-side EPUB optimiser that runs in
the browser before upload. Uploads use the WebSocket protocol; if the WebSocket
connection cannot be opened, the page falls back to HTTP upload for the rest of
the session.

### Settings

The Settings page exposes many firmware settings in the browser. It also has
cards for:

- Saved Wi-Fi networks
- OPDS servers

Up to eight Wi-Fi networks and eight OPDS servers can be stored. Passwords are
accepted when adding or editing entries, but saved passwords are never returned
by the API; omitting the password field on an update keeps the stored one.

### Fonts

The Fonts page lists installed SD-card font families and lets you upload
`.cpfont` files. The picker selects a folder, and the page refuses a selection
spanning more than one family, so upload one font family at a time. The server
validates the font family name, filename, and `CPFONT` magic bytes before
accepting the upload, and deletes the file if any check fails. Families are
written to `/.fonts/` or `/fonts/`, reusing the root the family already lives in;
a new family goes to `/.fonts/` unless only `/fonts/` exists.

Installed fonts appear in the **Reader Font Family** setting, under **Reader**,
after the font registry refreshes.

## Command Line Use

Power users can use `curl`, WebDAV clients, or WebSocket clients while the web
server is running.

Endpoint details are documented in [webserver-endpoints.md](./webserver-endpoints.md).

## Security Notes

- The HTTP server runs on port 80, and also answers WebDAV requests.
- The WebSocket upload server runs on port 81.
- A UDP discovery listener answers on port 8134.
- Hotspot mode also runs a captive-portal DNS server on UDP port 53 that answers
  every query with the reader's own address.
- There is no authentication and no TLS.
- Anyone on the same network can access the web interface while it is running.
- The server stops when you exit File Transfer or Calibre Wireless mode. The
  reader then restarts silently back to Home; on touch boards (`sticky`,
  `x4pro`, `papermono`) it switches Wi-Fi off in place instead of rebooting.
- Hotspot mode creates an open network for connectivity fallback; disconnect when done.
- Dot-prefixed names and the protected `System Volume Information` and `XTCache`
  entries cannot be downloaded, renamed, moved or deleted. Over HTTP the check
  looks at the item's own name, so a normally-named file inside a hidden folder
  is still reachable by its full path; WebDAV checks every segment instead.

## Tips

1. Use **Create Hotspot** when no trusted network is available.
2. Prefer `crosspoint.local` when available, but keep the displayed IP address as a fallback.
3. Move closer to the router if upload progress stalls in Join a Network mode.
4. Upload custom fonts through the Fonts page or copy them to `/.fonts/` or `/fonts/` on the SD card.
5. Exit File Transfer mode when finished to conserve battery.

## Related Documentation

- [User Guide](../USER_GUIDE.md)
- [Webserver Endpoints](./webserver-endpoints.md)
- [SD Card Fonts](./sd-card-fonts.md)
- [Troubleshooting](./troubleshooting.md)
