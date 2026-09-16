# CrossPoint User Guide

Welcome to the **CrossPoint** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [CrossPoint User Guide](#crosspoint-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
    - [Taking a Screenshot](#taking-a-screenshot)
    - [Frontlight (X4 Pro, Paper Mono)](#frontlight-x4-pro-paper-mono)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Recent Books Screen](#34-recent-books-screen)
    - [3.5 File Transfer Screen](#35-file-transfer-screen)
    - [3.5.1 Calibre Wireless Transfers](#351-calibre-wireless-transfers)
      - [Installing the Plugin in Calibre](#installing-the-plugin-in-calibre)
      - [Configuring the CrossPoint Plugin in Calibre](#configuring-the-crosspoint-plugin-in-calibre)
      - [Uploading Books](#uploading-books)
      - [Removing a Book](#removing-a-book)
    - [3.6 Settings](#36-settings)
      - [3.6.1 Display](#361-display)
      - [3.6.2 Reader](#362-reader)
        - [3.6.2.1 Text Settings](#3621-text-settings)
        - [3.6.2.2 Customise Status Bar](#3622-customise-status-bar)
      - [3.6.3 Controls](#363-controls)
      - [3.6.4 System](#364-system)
      - [3.6.5 OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries)
      - [3.6.6 Web Settings (Wi-Fi + OPDS)](#366-web-settings-wi-fi--opds)
      - [3.6.7 KOReader Sync Quick Setup](#367-koreader-sync-quick-setup)
        - [Option A: CrossPoint Sync Server (`sync.crosspointreader.com`, default)](#option-a-crosspoint-sync-server-synccrosspointreadercom-default)
        - [Option B: Legacy Public KOReader Server (`sync.koreader.rocks`)](#option-b-legacy-public-koreader-server-synckoreaderrocks)
        - [Option C: Self-Hosted Server (Docker Compose)](#option-c-self-hosted-server-docker-compose)
        - [Syncing While Reading](#syncing-while-reading)
    - [3.7 Sleep Screen](#37-sleep-screen)
      - [Cover settings](#cover-settings)
      - [Custom images](#custom-images)
    - [3.8 Custom Fonts (SD Card)](#38-custom-fonts-sd-card)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [Auto Page Turn](#auto-page-turn)
    - [Tilt Page Turn](#tilt-page-turn)
    - [Footnote Navigation](#footnote-navigation)
    - [Dictionary Lookup](#dictionary-lookup)
    - [System Navigation](#system-navigation)
    - [Other Book Formats](#other-book-formats)
    - [Supported Languages](#supported-languages)
  - [5. Reader Menu](#5-reader-menu)
    - [5.1 Chapter Selection](#51-chapter-selection)
    - [5.2 Bookmarks](#52-bookmarks)
  - [6. Current Limitations \& Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues \& Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)

## 1. Hardware Overview

The device utilises the standard buttons on the Xteink X4 (in the same layout as the manufacturer firmware, by default):

### Button Layout

| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Side Up**, **Side Down**, **Reset** |

Button layout can be customized in the **[Controls Settings](#363-controls)**.

> [!NOTE]
> The table above describes the Xteink X4 and X3. Other supported boards wire a different key set — the X4 Classic drops the touchscreen and frontlight and reuses their pins as four extra discrete front keys, the Sticky shares one key between Confirm and Power (click confirms, hold sleeps), and Paper Mono has only two keys, with holds standing in for Back, Confirm and Power. The firmware works in logical buttons (Back, Confirm, Left, Right, Page Back, Page Forward), so the on-screen hints always name what the keys currently do.

### Taking a Screenshot

When the Power button and the lower side button (Side Down) are pressed at the same time, it will take a screenshot. The combination reads the physical side key, so it works regardless of how the buttons are remapped, and the screen flashes a border for a second to confirm.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.

Screenshots are written to the `screenshots/` folder on the SD card as 1-bit BMP files:

* Outside a book: `screenshots/screenshot-<milliseconds>.bmp`.
* While reading: a per-book subfolder, `screenshots/<Title>/<Title>_ch<chapter>_p<page>_<percent>pct_<milliseconds>.bmp` in the EPUB reader, and the same name without the `ch<chapter>` part in the other readers. The title is sanitised for FAT32, and a name that would push the path past the 255-character limit is shortened.

### Frontlight (X4 Pro, Paper Mono)

Of the supported boards, the X4 Pro and Paper Mono have a built-in frontlight. The X4 Pro's light has two channels, so it offers both brightness and warmth; Paper Mono's is single-channel, so only brightness is shown. It is controlled from a panel rather than the Settings menu:

* **Open the frontlight panel:** Swipe down from the top edge of the screen, from almost any screen (Home, Browse Files, Reading Mode, etc.). On touch boards you can also tap the status bar band at the top of Home, Browse Files, Settings or File Transfer, which is more reliable on panels with etched glass. Drag the brightness (and, on the X4 Pro, warmth) slider to adjust the light live, or tap the sun icon to turn it on or off.
* **Quick toggle (X4 Pro only):** Double-click the **Power** button to turn the frontlight on or off instantly, without opening the panel.

> [!NOTE]
> Frontlight brightness and warmth are intentionally not listed in **[Display Settings](#361-display)** — the panel is the only place to adjust them. On the X4 Pro the on/off state can also be toggled with the Power-button double-click above.

If the frontlight doesn't come back on after the device wakes from sleep, check **Restore Light on Wake** in **[Display Settings](#361-display)** (on by default). Turning it off is intentional if you'd rather have the light stay off on wake and switch it on yourself each time — but it's easy to forget you changed it.

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.
In the **[Controls Settings](#363-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE]
> On subsequent restarts, the firmware reopens the last book you were reading — but only when all of the following hold: a book path is recorded, the device went to sleep from Reading Mode, the previous automatic reopen finished without crashing, and the **Back** button is not held down while the device starts. Otherwise it boots to **[Home](#31-home-screen)**. Holding **Back** during startup is therefore a reliable way to land on Home instead of the book.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. From here you can navigate to **[Reading Mode](#4-reading-mode)** with the most recently read book, the **[Browse Files](#33-browse-files-screen)** screen, the **[Recent Books](#34-recent-books-screen)** screen, the **[File Transfer](#35-file-transfer-screen)** screen, or **[Settings](#36-settings)**.

* **Continue Reading:** The top of the screen shows the most recently read book as a cover tile with its title. Books whose file is no longer on the SD card are skipped. The layout follows the **[UI Theme](#361-display)**: "Lyra Extended" shows three books side by side instead of one, and "RoundedRaff" shows a cover-only tile, puts the book title in the header band and adds a **Continue Reading** row to the menu. Missing cover thumbnails are generated in the background after the screen first appears, with a progress popup.
* **OPDS Browser:** An extra **OPDS Browser** row appears between **Recent Books** and **File Transfer**, but only once at least one server has been added in **[OPDS Servers](#365-opds-servers-multiple-libraries)**.
* **Navigation:** **Left**/**Right** (or **Side Up**/**Side Down**) move the cursor and wrap around at both ends; **Confirm** activates the selected tile or row. On touch boards you can also swipe vertically or tap directly.
* **Resume:** Pressing **Back** on the Home screen opens the most recently read book straight away — the button hint reads **Resume**. If there are no recent books, the hint is blank and **Back** does nothing.

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen acts as a file and folder browser. The header names the current folder ("SD card" at the root), and the **full path** is shown in a band along the **bottom** of the screen, truncated from the left with an ellipsis so the deepest folder always stays readable. Folders are listed before files, in natural order. File extensions are displayed as a value at the right of each row.

Only readable file types are listed: `.epub`, `.fb2` *(fork-only)*, `.xtc`/`.xtch`, `.txt`, `.md`, `.bmp` and `.png`. Everything else is hidden, as is the `System Volume Information` folder. Entries whose name starts with `.` — including the firmware's own `.crosspoint` cache folder — are hidden unless **Show Hidden Files** is turned on in **[System Settings](#364-system)**.

How folders are drawn depends on the theme: in "Classic" and "RoundedRaff" they appear in brackets (e.g. `[folder-name]`), while the icon-based themes ("Lyra" and "Lyra Extended") show a folder icon and the bare name.

* **Navigate List:** Use **Left** (or **Side Up**), or **Right** (or **Side Down**) to move the selection cursor up and down through folders and books. You can also long-press these buttons to scroll a full page up or down.
* **Open Selection:** Press **Confirm** to open a folder or start reading a selected book. Selecting a `.bmp` or `.png` file will open the image viewer.
* **Go Up / Go Home:** A short **Back** goes up one directory, with the cursor placed on the folder you just left; at the root, **Back** returns to the **[Home](#31-home-screen)** screen. Holding **Back** for a second jumps straight back to the root folder.
* **Delete Files or Folders:** Hold **Confirm** for a second and release to delete the selected file or folder (on touch boards, long-press the row instead). You will be given an option to either confirm or cancel. Deleting a folder removes everything inside it, and the cached data for each deleted book is cleared along with it. Files are deleted one at a time — there is no multi-select.

> [!NOTE]
> Renaming and moving files is not available on the device. Use the **[File Transfer](#35-file-transfer-screen)** web interface for that.

### 3.4 Recent Books Screen

The Recent Books screen lists the most recently opened books in a chronological view, most recent first, displaying title and author. At most **ten** books are kept; opening an eleventh drops the oldest entry.

Entries whose file is no longer on the SD card are dropped automatically whenever the screen is opened or a book is added, so deleting a book from **[Browse Files](#33-browse-files-screen)** or over the web also clears it from this list.

* **Open:** Press **Confirm** (or tap the row) to open the selected book.
* **Remove from List:** Hold **Confirm** for a second, or long-press the row on touch boards, then confirm. This removes the entry from the list only — the book file itself stays on the SD card.
* **Back:** Returns to the **[Home](#31-home-screen)** screen.

The list is stored on the SD card in `.crosspoint/recent.json`.

### 3.5 File Transfer Screen

The File Transfer screen allows you to upload and manage files on the device. When you enter the screen, choose **Join a Network**, **Calibre Wireless**, or **Create Hotspot**. The reader then starts the web server for the selected mode. Pressing **Back** on the mode list returns to **[Home](#31-home-screen)**; cancelling the Wi-Fi network picker returns to the mode list.

On boards built with USB mass-storage support — the X4 Pro, X4 Classic and Paper Mono — a fourth mode, **USB Drive**, is also offered. It hands the raw SD card to a computer over the USB cable, so the card appears as a removable drive and you can copy files onto it directly. The reader shows a "Connect this reader to your computer" prompt while it waits (connecting can take up to 30 seconds), and returns Home when you eject the drive or unplug the cable. While USB Drive is active the firmware cannot read the card itself, so the rest of the interface is unavailable until it ends.

See the [web server docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

The web interface also supports **WebDAV**, allowing you to mount the device as a network drive and manage files directly from your computer's file manager. It is also where you rename or move files, which the on-device **[Browse Files](#33-browse-files-screen)** screen does not offer.

Download links for files already on the device are available in the web interface, so you can retrieve books or screenshots over Wi-Fi without connecting a cable.

While the web server is running in joined-network mode, the network name is shown under the header with a **four-bar Wi-Fi signal indicator** on the right. The bars fill according to the current signal strength (with a little hysteresis so they do not flicker), and are replaced by a cross if the connection drops. The indicator is not shown in hotspot mode. If the link stays down for five minutes, the reader gives up and returns Home.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [web server docs](./docs/webserver.md) for details.
> [!TIP]
> If your EPUBs have compatibility issues, tick **Optimize EPUB** in the web interface's upload dialog. The optimisation runs **in your browser** before the file is sent — it converts images to grayscale JPEG (85% quality by default, adjustable under Advanced Mode), caps them at 480×800 pixels and fixes SVG wrappers — so no work is done on the reader itself. An optional **Rename from Book Metadata** switch stores the upload as `Title - Author.epub`.

### 3.5.1 Calibre Wireless Transfers

CrossPoint supports sending books from Calibre using the CrossPoint Reader device plugin.

#### Installing the Plugin in Calibre

If you don't already have the plugin installed:

1. Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.
2. Download the zip file.
3. Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.
4. Restart Calibre.

#### Configuring the CrossPoint Plugin in Calibre
1. In Calibre select Preferences.
2. In the Preferences dialog select Plugins.
3. In Plugins search for "crosspoint".
4. Click on "Customize plugin".
5. Update the value for "Host" to match the IP for your device.
6. Leave the other settings as they are.
7. [optional] Modify the "Upload path" to point to a subfolder other than the root "/" folder. Enter this as a path relative to the root folder. Example: `/mybooks`
8. Restart Calibre.

<img width="420" height="385" alt="Image" src="https://github.com/user-attachments/assets/01fc7e33-a9a7-48ba-9e26-2e68d1f9daec" />

#### Uploading Books

To upload a book using the CrossPoint plugin in Calibre:

1. On the device: File Transfer -> Calibre Wireless, then join a network.
2. Select one or more books.
3. Right-click on that selection.
4. Select "Send to Device" > "Send to main memory"

The CrossPoint plugin will connect to your device, create a folder for the book's author in the root folder (or the folder you configured for the plugin), then copy the book into that folder.

<img width="783" height="310" alt="Image" src="https://github.com/user-attachments/assets/741b0909-2e1d-4f16-8af0-2c43fbda5ce6" />

#### Removing a Book

Books cannot be removed from your device through Calibre. Use the web interface instead.

### 3.6 Settings

The Settings screen is split into four tabs — **Display**, **Reader**, **Controls** and **System** — and always opens on **Display**. Two-option settings toggle in place; settings with more than two options open a picker. Every change is saved as soon as it is made.

Some rows only exist on hardware that supports them, and are marked below.

#### 3.6.1 Display

- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  
  - "Dark" (default) - The default dark Crosspoint logo sleep screen
  - "Light" - The same default sleep screen, on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#37-sleep-screen) below for more information
  - "Cover" - The book cover image (Note: this is experimental and may not work as expected)
  - "Cover + Custom" - The book cover image while actively reading, falls back to "Custom" behavior otherwise
  - "None" - A blank screen
  - "Quick Resume" - The screen you were last looking at stays on the panel and a moon icon is drawn in the corner. The frame is saved to the SD card and restored on wake, so reading resumes without reloading the book.
  - "Transparent" - A transparent overlay image drawn over the current screen; see [Sleep Screen](#37-sleep-screen) below for more information

- **Sleep Screen Cover Mode**: How to display the book cover when "Cover" sleep screen is selected:
  
  - "Fit" (default) - Scale the image down to fit centered on the screen, padding with white borders as necessary
  - "Crop" - Scale the image down and crop as necessary to try to fill the screen (Note: this is experimental and may not work as expected)

- **Sleep Screen Cover Filter**: What filter will be applied to the book cover when "Cover" sleep screen is selected:
  
  - "None" (default) - The cover image will be converted to a grayscale image and displayed as it is
  - "Contrast" - The image will be displayed as a black & white image without grayscale conversion
  - "Inverted" - The image will be inverted as in white & black and will be displayed without grayscale conversion

- **Quick Resume on Timeout**: Whether a sleep caused by inactivity (System > Time to Sleep) uses the "Quick Resume" screen instead of the configured sleep screen; options are "On" or "Off" (default). Selecting the "Quick Resume" sleep screen switches this on for you, and switching away from it switches it back off again unless you had turned it on yourself.

- **Hide Battery %**: Configure where to suppress the battery percentage display in the status bar; the battery icon will still be shown:
  
  - "Never" (default) - Always show battery percentage
  - "In Reader" - Show battery percentage everywhere except in reading mode
  - "Always" - Always hide battery percentage

- **Refresh Frequency**: How often the screen does a full refresh while reading to reduce ghosting:
  
  - "1 page", "5 pages", "10 pages", "15 pages" (default), "30 pages"
  - "Never" - Never promote a page turn to a full refresh. Ghosting will accumulate; a manual refresh (see **Short Power Button Click** below, or the quick panel on touch models) is then the only way to clear it.

- **UI Theme**: Set which UI theme to use:
  
  - "Classic" - The original Crosspoint theme
  - "Lyra" (default) - The theme for Crosspoint featuring rounded elements and menu icons
  - "Lyra Extended" - Lyra, but displays 3 books instead of 1 on the **[Home Screen](#31-home-screen)**
  - "RoundedRaff" - A rounded theme with additional visual styling

- **Sunlight Fading Fix**: A software fix for the issue where white X4 models may fade when used in direct sunlight; options are "On" or "Off" (default). *Only shown on the X4/X3 build — the fix is a grayscale-waveform compensation that does not apply to the touch models or to the X4 Classic.*

- **Restore Light on Wake**: Whether the frontlight returns to its saved on/off state after a wake or a normal boot; options are "On" (default) or "Off". Brightness and warmth are always remembered either way. *Frontlight models only (X4 Pro, Paper Mono).*

- **Night Mode**: Inverts the whole display — white text on black — for every screen; options are "On" or "Off" (default). The sleep screen always draws in normal polarity. Night Mode can also be toggled from the reader menu, and from the quick panel on touch models.

> [!NOTE]
> A lightning bolt is drawn inside the battery icon whenever the device is connected to USB power.

> [!NOTE]
> The status bar shown while reading is configured under **Reader > Customise Status Bar**, not here.

#### 3.6.2 Reader

- **Text Settings**: Opens a dedicated screen with a live preview of your settings; see [Text Settings](#3621-text-settings) below.

- **Manage Fonts**: Browse, download, update and delete custom font families over Wi-Fi. Installed families are marked "Installed" (or "Update" when a newer build exists), and Confirm on an up-to-date installed family offers to delete it. See [Custom Fonts (SD Card)](#38-custom-fonts-sd-card) for more information.

- **Reading Orientation**: Set the screen orientation used in Reading Mode:
  
  - "Portrait" (default) - Standard portrait orientation
  - "Landscape CW" - Landscape, rotated clockwise
  - "Portrait 180°" - Portrait, upside down
  - "Landscape CCW" - Landscape, rotated counter-clockwise
  
  XTC books always render in portrait and ignore this setting.

- **Images**: How embedded images (JPG/PNG) in EPUB files are handled:
  
  - "Display" (default) - Decode and draw the image
  - "Placeholder" - Skip the image and show its alt text as `[Image: ...]` in italics; images with no alt text are dropped
  - "Suppress" - Remove images entirely

- **Reader Menu Style**: How the reader menu opens; options are "List" (default), the full-screen menu, or "Toolbar", an overlay painted over the page. *Touch models only (Sticky, X4 Pro, Paper Mono).*

- **Dictionary**: Select the StarDict dictionary used for word lookups while reading, or "None" to disable lookups. *(Only shown when at least one dictionary folder exists under `/dictionaries/` on the SD card — see [docs/dictionary.md](docs/dictionary.md) for setup and usage.)*

- **Customise Status Bar**: Opens the status bar sub-screen; see [Customise Status Bar](#3622-customise-status-bar) below.

##### 3.6.2.1 Text Settings

**Text Settings** is a four-tab screen (**Font**, **Size**, **Layout**, **Style**) with a live preview pane at the top, so each change is visible before you leave the screen. Changes are saved as you make them.

- **Font**: Noto Serif (default), Noto Sans, and every custom family installed from the SD card. The currently active family is marked "Selected".

- **Size**: The point sizes the active family actually ships, listed as "12 pt", "14 pt" and so on. The built-in families are compiled at 12, 14, 16 and 18 pt; the default is 14 pt. A custom family offers whatever sizes its `.cpfont` files were built at, and switching family snaps your size to the nearest available one.

- **Layout**:
  
  - **Line Spacing**: "Tight", "Normal" (default), "Wide" or "Extra Wide".
  - **Extra Paragraph Spacing**: "On" (default) adds vertical space between paragraphs; "Off" removes it and indents the first line instead.
  - **Alignment**: "Justify" (default), "Left", "Center", "Right", or "Book's Style" — which follows the CSS `text-align` of the book itself and falls back to justified where the book sets none.
  - **Screen Margin**: 5 to 40 pixels in 5-pixel steps (default 5).

- **Style**:
  
  - **Focus Reading**: Bolds the first part of each word to create visual fixation points, similar to Bionic Reading; "On" or "Off" (default).
  - **Hyphenation**: Hyphenate text in Reading Mode; "On" or "Off" (default).
  - **Embedded Style**: Use the EPUB file's embedded HTML and CSS stylisation and formatting; "On" (default) or "Off".
  - **Text Anti-Aliasing**: Smooth grey edges on text; "On" (default) or "Off". Note this slows down page turns slightly.

##### 3.6.2.2 Customise Status Bar

This sub-screen configures the status bar drawn while reading, with a live preview. Corrupt or migrated values are reset to their defaults when the screen is opened.

- **Chapter Page Count**: Show the page number within the chapter; "On" (default) or "Off".
- **Book Progress Percentage**: Show progress through the whole book as a percentage; "On" (default) or "Off".
- **Progress Bar**: "Book", "Chapter" or "Hide" (default).
- **Progress Bar Thickness**: "Thin", "Medium" (default) or "Thick".
- **Title**: "Book", "Chapter" (default) or "Hide".
- **Battery**: Show the battery icon; "On" (default) or "Off".
- **XTC Status Bar**: Where the status bar sits in XTC books — "Hide" (default), "Bottom" or "Top".
- **Clock**, **Clock Format**, **Clock UTC Offset**, **Sync clock now**: *Only shown on models with a real-time clock (X3, Sticky, X4 Pro, X4 Classic, Paper Mono).* Clock position is "Hide" (default), "Right" or "Left"; format is "24-hour" (default) or "12-hour"; the UTC offset is set in quarter-hour steps from UTC-12:00 to UTC+14:00; and "Sync clock now" fetches the time over Wi-Fi.

#### 3.6.3 Controls

- **Remap Front Buttons**: A menu for customising the function of each bottom edge button. *Button models only.*

- **Side Button Layout (reader)**: Swap the order of the up and down side buttons from "Prev/Next" (default) to "Next/Prev", or select "Disabled" to turn them off. This change is only in effect when reading.

- **Touch Reader Controls**: How the touchscreen turns pages while reading. *Touch models only.*
  
  - "Off" - No touch page turns
  - "Tap" - Tap the left third to go back, the right third to go forward
  - "Swipe" (default) - Swipe left/right to turn pages; taps stay free for the reader menu
  - "Inverted Tap" - Tap zones reversed

- **Show Reader Menu**: How the reader menu opens by touch — "Off", "Tap" (default, a tap in the centre third) or "Swipe Up" (from the bottom edge). *Only shown on models with a capacitive home key (X4 Pro); elsewhere it stays on "Tap".*

- **Orient front buttons**: Whether the front Left/Right pair follows the reading orientation; options are "On" or "Off" (default). *Button models only.*

- **Long-press button behavior**: What a long hold (700 ms) of a page-turn button does while reading:
  
  - "Off" (default) - No long-press action. Page turns then fire on press rather than on release, which feels faster.
  - "Chapter skip" - Hold to skip to the next/previous chapter
  - "Orientation change" - Hold to rotate the reading orientation

- **Long-press Menu**: The function bound to holding Confirm while reading (and to holding the home key on models that have one). A short press of Confirm always opens the reader menu as normal.
  
  - "KOSync" - Hold Confirm (1 second) to launch KOReader sync directly. Only active once sync credentials are saved.
  - "Disabled" (default) - Long-press is ignored; only short-press opens the reader menu.
  - "Bookmark" - Hold Confirm (0.4 second) to drop a bookmark at the current page.
  - "Dictionary" - Hold Confirm (0.4 second) to start dictionary word selection on the current page (see [docs/dictionary.md](docs/dictionary.md)).
  - "Reader Menu" - Hold to open the reader menu. *Only offered on models with a capacitive home key (X4 Pro), where the hold is on that key.*

- **Short Power Button Click**: Controls the effect of a short click of the power button:
  
  - "Ignore" (default) - Require a long press to turn off the device
  - "Sleep" - A short press puts the device into sleep mode
  - "Page Turn" - A short press in reading mode turns to the next page; a long press turns the device off
  - "Refresh Screen" - A short press forces a full-panel refresh, useful for clearing ghosting
  - "Footnotes" - A short press in reading mode opens the footnotes of the current page; if only one footnote is present, the referenced page is opened directly
  - "Confirm" - A short press acts as the Confirm button. *Touch models only.*

- **Tilt Page Turn**: Turn pages by tilting the device; options are "Off" (default), "Normal" or "Inverted". *Only shown on models with a motion sensor (X3, Sticky, X4 Classic).*

- **Quick-return from footnotes**: Shown only while **Short Power Button Click** is "Footnotes". When on (the default), a short press of the power button while reading a footnote returns to the page you came from, like the back button. When off, a short press inside a footnote behaves like any other Footnotes click: it opens the footnotes of the page you are on.

- **Short Back to File Browser**: A short press of Back in the reader goes to the file browser instead of Home; options are "On" or "Off" (default). *Button models only.*

#### 3.6.4 System

- **Time to Sleep**: Set the duration of inactivity before the device automatically goes to sleep. Selecting the row opens a slider that runs from 1 to 30 minutes (default 10), with one further step past 30 labelled "Never", which disables the inactivity timeout entirely.

- **Show Hidden Files**: Show files and directories whose names begin with `.` in the file browser; options are "On" or "Off" (default).

- **Clear Read Books from Recent List**: Remove a book from the Recent Books list once its End-of-Book screen is reached; options are "On" or "Off" (default).

- **Move Finished Books to Read Folder**: Move a finished EPUB into a `/read` folder on the SD card; options are "On" or "Off" (default).

- **Wi-Fi Networks**: Connect to Wi-Fi networks for file transfers and firmware updates.

- **KOReader Sync**: Options for setting up KOReader for syncing book progress. **Smart sync** is the default for new configurations and auto-resolves simple push/pull decisions. Existing credential files retain **Ask every time** when migrated; you can switch Sync Behavior at any time if you prefer manual confirmation.

- **OPDS Servers**: Manage one or more OPDS [(Open Publication Distribution System)](https://en.wikipedia.org/wiki/Open_Publication_Distribution_System) libraries for browsing and downloading books. See [OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries) below.

- **Clear Reading Cache**: Clear the internal SD card cache. A warning is shown first and the clear has to be confirmed.

- **Check for updates**: Check for Crosspoint firmware updates over Wi-Fi. Each model fetches its own release asset, so a model whose build has not been published yet simply reports that no update is available.

- **SD Card Firmware Update**: Flash a firmware `.bin` file from the SD card, with no USB or Wi-Fi needed. A `.bin`-only file picker opens; the selected file is validated (size, magic byte, chip and board identity, checksum) and you are asked to confirm before it is written. The device restarts into the new firmware when the write completes, and a failure is reported by name rather than leaving the device half-flashed.

- **Language**: Set the UI language. CrossPoint ships 34 interface languages, with English as the reference; anything a translation is missing falls back to English.

- **Keyboard Layouts**: Choose which on-screen keyboard layouts are reachable when entering text. Nine layouts are listed (English, French, German, Spanish, Russian, Ukrainian, Belarusian, Kazakh and Hebrew), each "On" or "Off". At least one Latin layout (English, French, German or Spanish) must stay enabled, so the last of those still on is locked and shown as "Default". If you never touch this screen, CrossPoint enables the UI language's layout plus English.

#### 3.6.5 OPDS Servers (Multiple Libraries)

CrossPoint supports saving multiple OPDS servers and switching between them when browsing catalogs.

1. Open **Settings -> System -> OPDS Servers**.

2. Select **Add Server** to create a new entry, or select an existing server to edit it.

3. Configure these fields. The editor prints the reminder *"For Calibre, add /opds to your URL"* above them, and every field is written to the SD card the moment you leave the keyboard, so a half-configured server survives navigation or a power loss.
   
   - **Server Name**: Optional display name, up to 63 characters (for example, "Home Calibre" or "Public Catalog"). When it is empty the list shows the URL instead.
   
   - **OPDS Server URL**: Full catalog root URL, up to 127 characters. The keyboard opens prefilled with `https://`; saving nothing but that bare scheme clears the URL again.
   
   - **Username / Password**: Optional credentials for authenticated servers, up to 63 characters each. A saved password is shown as `******`, never in plain text.

4. Use **Delete Server** inside a server entry to remove it. The row only exists for servers that have already been saved.

Two more rows sit below the server list:

- **Download folder**: Where downloaded books are written. What you type is normalised to a leading-slash path (`books` becomes `/books`, trailing slashes are dropped), and an empty value — or a bare `/` — means the SD card root and is shown as "SD root". A missing folder is created on the first download; if that fails the book lands in the SD root instead.
- **Filename format**: How a downloaded book is named: **Author - Title** (default), **Title - Author**, or **Title**. A book with no author collapses to the title in all three. The result is sanitised for FAT, capped at 100 bytes, and given the `.epub` extension; a file of the same name is overwritten.

Behavior notes:

- You can store up to 8 OPDS servers.
- OPDS requests use preemptive HTTP Basic auth: the header is sent with the very first request, without waiting for a `401` challenge. Credentials are sent **only when both Username and Password are filled in** — a username on its own is ignored. If you use Calibre Content Server with authentication enabled, set it to Basic (not Digest).
- An **OPDS Browser** entry appears on the Home screen once at least one server is saved. With exactly one server it opens that catalog directly; with several it opens the same list as a picker, showing only the servers and **Add Server**, where selecting a server starts browsing it instead of editing it.

You can also manage OPDS servers from the web interface while in File Transfer mode:

1. Connect to the device web UI.
2. Open `http://<device-ip>/settings`.
3. Use the **OPDS Servers** card to add, edit, or delete entries.

For web-based Wi-Fi network management, see [Web Settings (Wi-Fi + OPDS)](#366-web-settings-wi-fi--opds).

#### 3.6.6 Web Settings (Wi-Fi + OPDS)

While in **File Transfer** mode, the web settings page includes management cards for both **Wi-Fi Networks** and **OPDS Servers**.

1. On device: open **File Transfer** and connect through **Join a Network** or **Create Hotspot**.
2. In a browser, open `http://<device-ip>/settings` or `http://crosspoint.local/settings`.
3. In **Wi-Fi Networks**, add, edit, or delete saved network entries (SSID + optional password).
4. In **OPDS Servers**, add, edit, or delete OPDS catalogs.

Behavior notes:

- The **Password** boxes in these cards are the Wi-Fi network's or the OPDS catalog's own credential. They do not protect the device: the web interface itself has no login, so every page and API it serves is reachable by anyone on the same network or hotspot for as long as File Transfer mode is running.
- Passwords are never sent back to the browser. The API reports only whether one is set, and the box then shows an `(unchanged)` placeholder.
- Leaving Password blank while editing keeps the existing saved password unchanged; typing a new one replaces it.
- The web UI can save hidden-network SSIDs. On the device, the Wi-Fi list always ends with an entry for joining a network by typing its SSID, which is how a hidden AP is reached.

#### 3.6.7 KOReader Sync Quick Setup

CrossPoint can sync reading progress with KOReader-compatible sync servers.
It also interoperates with KOReader apps/devices when they use the same server and credentials.

**Settings -> System -> KOReader Sync** holds these rows. Each one is written to the SD card the moment it changes:

- **Username** and **Password**: Up to 64 characters each. Enter the plain password; the row shows `******` once it is set, and CrossPoint derives the MD5 key the protocol wants internally.
- **Sync Server URL**: Up to 128 characters. Leaving it empty means `https://sync.crosspointreader.com`, and the row then reads "Default: sync.crosspointreader.com". As in the OPDS editor, saving nothing but the prefilled `https://` clears the field back to empty.
- **Document Matching**: **Filename** (the default) hashes the file's name, so the same book matches across devices even when the files are not byte-identical. **Binary** hashes 1 KB samples taken at fixed offsets through the file — the partial-MD5 scheme KOReader itself uses to identify documents — so it survives renaming the file but needs byte-identical copies.
- **Send Document Metadata**: Off by default. When on, an upload also carries the book's filename, title and authors for servers that display them.
- **Sync Behavior**: **Ask every time** or **Smart sync** (see [Syncing While Reading](#syncing-while-reading) below). A device set up from scratch starts on Smart sync; a configuration file written by an older firmware that never had this setting is read back as Ask every time.
- **Sign Up** and **Authenticate**: Both do nothing until a username *and* a password are saved; until then the row shows "[Set credentials first]".

Document Matching, Send Document Metadata and Sync Behavior each have two values, so selecting one flips it in place instead of opening a picker.

##### Option A: CrossPoint Sync Server (`sync.crosspointreader.com`, default)

When **Sync Server URL** is left empty, CrossPoint uses the free CrossPoint sync server at `https://sync.crosspointreader.com`. It speaks the standard KOReader sync protocol (so KOReader apps can use it too). Uploads to this server carry one extra, CrossPoint-only `position` object alongside the standard fields — quantised percentage, spine index, page number, page count, paragraph index and the same XPath (only when it fits in 120 bytes) — so two CrossPoint devices with identical settings land on exactly the same page. That object is never sent to any other server.

1. On each CrossPoint device:

   - Go to **Settings -> System -> KOReader Sync**.

   - Set **Username** and **Password** (enter the plain password; CrossPoint computes MD5 internally, and use the same values on all devices).

   - Leave **Sync Server URL** empty (or set it to `https://sync.crosspointreader.com`).

   - On the first device, run **Sign Up** once to create the account directly from the device. On every other device, just run **Authenticate**.

Accounts are per server. Existing `sync.koreader.rocks` credentials do not exist on the CrossPoint server; either sign up again with the same username/password or use Option B to keep using the legacy server.

##### Option B: Legacy Public KOReader Server (`sync.koreader.rocks`)

Use this if you already sync KOReader devices against the official public server.

If your device was already syncing with an older CrossPoint build that had credentials saved and no explicit server URL, the upgrade pins the URL to `https://sync.koreader.rocks:443` for you, so it keeps talking to the same server rather than silently moving to the CrossPoint one.

1. On each CrossPoint device:

   - Go to **Settings -> System -> KOReader Sync**.

   - Set **Sync Server URL** to `https://sync.koreader.rocks` (required; an empty URL now points at the CrossPoint server instead).

   - Set **Username** and **Password** to your existing KOReader Sync credentials.

   - Run **Authenticate**.

2. If you do not have an account yet, run **Sign Up** on the device, or register once with curl:

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "https://sync.koreader.rocks/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

When this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, pick a different username or use that existing account.

##### Option C: Self-Hosted Server (Docker Compose)

1. Start a sync server:

```bash
mkdir -p kosync-quickstart
cd kosync-quickstart

cat > compose.yaml <<'YAML'
services:
  kosync:
    image: koreader/kosync:latest
    ports:
      - "7200:7200"
      - "17200:17200"
    volumes:
      - ./data/redis:/var/lib/redis
    environment:
      - ENABLE_USER_REGISTRATION=true
    restart: unless-stopped
YAML

# Docker
docker compose up -d

# Podman (alternative)
podman compose up -d
```

> [!NOTE]
> `ENABLE_USER_REGISTRATION=true` is convenient for first setup. After creating your users, set it to `false` (or remove it) to avoid unexpected registrations.

2. Verify the server:

```bash
curl -H "Accept: application/vnd.koreader.v1+json" "http://<server-ip>:17200/healthcheck"
# Expected: {"state":"OK"}
```

3. Register a user once.
   CrossPoint authenticates against KOReader Sync (`koreader/kosync`) using an MD5 key, so register using the MD5 of your password:

> [!WARNING]
> Sending a reusable MD5-derived password over plain HTTP is insecure.
> Create unique sync-only credentials and do not reuse main account passwords.
> Prefer `https://<server-ip>:7200` whenever traffic leaves a fully trusted LAN or when using untrusted networks.
> Use `curl -k` only for self-signed certificate testing.

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "http://<server-ip>:17200/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

If this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, the account already exists.

4. On each CrossPoint device:
   
   - Go to **Settings -> System -> KOReader Sync**.
   
   - Set **Username** and **Password** (enter the plain password; CrossPoint computes MD5 internally, and use the same values on all devices).
   
   - Set **Sync Server URL** to `http://<server-ip>:17200`.
   
   - Run **Authenticate**.

If you use the HTTPS listener, use `https://<server-ip>:7200` (`curl -k` only for self-signed certificate testing).

##### Syncing While Reading

Once any of the options above is set up, press **Confirm** while reading to open the reader menu, then select **Sync Progress**. Alternatively, set **Settings -> Controls -> Long-press Menu** to **KOSync** and hold Confirm for one second to launch sync directly.

Both entry points need saved credentials: with none, **Sync Progress** simply returns you to the page and the Confirm hold is not armed at all.

- With **Sync Behavior** set to **Ask every time**, CrossPoint shows both positions and offers **Apply remote progress** or **Upload local progress**, preselecting whichever is further ahead. When the server has nothing stored it shows "No remote progress found" and offers the upload instead.
- With **Sync Behavior** set to **Smart sync**, CrossPoint resolves the case itself: upload when no remote progress exists for any of the book's ids, report "Already synced" and change nothing when the two positions differ by no more than 0.1 percentage point, upload when local is further ahead, or apply remote when remote is.
- Smart sync also probes the **other** Document Matching method. It hashes the book both ways and, when the alternate id hashes to something different, asks the server for that record too, adopting it when the primary has no record or the alternate is further ahead. An upload afterwards still goes out under the id your Document Matching setting selects, so that record catches up. **Ask every time** never makes this second request.

How a position travels:

- What is uploaded is a standard KOReader xpointer, built from the page's own content rather than from a page number. CrossPoint first resolves the paragraph the page starts on to its real element path in the chapter's XHTML (`/body/DocFragment[N]/body/div[2]/p[4]`, with the fragment number being the 1-based spine position). Failing that it converts the fraction of the way through the chapter into a path with a codepoint offset (`.../p[4]/text()[1].96`), and if the chapter will not parse cleanly it counts `<p>` elements by byte offset and sends `/body/DocFragment[N]/body/p[N]`. The start of a chapter is always `/body/DocFragment[N]/body`.
- An incoming xpointer is resolved back to a character offset in the chapter's visible text and converted into a page using the local layout cache, so a device with different fonts, margins or orientation still lands on the right text. Only when that fails does CrossPoint fall back to the percentage (and, on the CrossPoint server, the `position` object's page hints).
- Every request carries `Accept: application/vnd.koreader.v1+json`, the `x-auth-user` and `x-auth-key` (MD5) headers KOReader uses, **and** an HTTP Basic `Authorization` header built from the same username and password, which is what lets Calibre-Web-Automated's sync endpoint accept the same account. Sign-up is the exception: it posts the username and MD5 password as a JSON body, sending only `Accept` and `Content-Type`.
- Sync is refused before the TLS handshake if the free heap has fallen too low, and reports a failure rather than risking the connection.

### 3.7 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode               | Behavior                                                                                                                     |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| **Dark** (default) | The CrossPoint logo on a dark background.                                                                                    |
| **Light**          | The CrossPoint logo on a white background.                                                                                   |
| **Custom**         | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found.                             |
| **Cover**          | The cover of the currently open book. Falls back to **Dark** if no book is open.                                             |
| **Cover + Custom** | The cover of the currently open book, shown only while actively reading. Falls back to **Custom** behavior when not reading. |
| **None**           | A blank screen.                                                                                                              |
| **Quick Resume**   | The screen stays as it was, with a moon icon drawn in the corner and no "Entering sleep" popup. The frame is saved to the SD card and restored on wake, so you return to the same page without reloading the book. |
| **Transparent**    | A BMP or PNG overlay drawn over the current screen. Supports PNG and 32-bit BGRA alpha transparency, and treats white as transparent in regular BMPs. Falls back to **Dark** if no valid overlay image is found. |

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom** or **Cover + Custom**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` images inside. One will be randomly selected each time the device sleeps. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. This takes priority over the `.sleep`/`sleep` directories.

#### Transparent overlay images

To use transparent sleep overlays, set the sleep screen mode to **Transparent**, then place BMP or PNG files on the SD card:

- **Multiple Images (recommended):** Create a `.sleep-overlay` directory in the root of the SD card and place any number of valid overlay `.bmp` or `.png` images inside. One will be randomly selected each time the device sleeps. A directory named `sleep-overlay` is also accepted as a fallback.
- **Single Image:** Place `sleep-overlay.bmp` or `sleep-overlay.png` in the root directory. A root BMP takes priority over a root PNG, and both take priority over the `.sleep-overlay`/`sleep-overlay` directories.

Transparent overlay files are intentionally separate from normal sleep images. Regular BMP formats supported by CrossPoint are accepted; white pixels leave the existing screen unchanged. For per-pixel alpha transparency, use a PNG with an alpha channel or a 32-bit BGRA BMP with both visible and non-opaque pixels. Opaque white pixels in alpha images erase the content behind them.

> [!TIP]
> For best results:
> - For non-transparent **Custom** mode, use uncompressed BMP files with 24-bit color depth.
> - For **Transparent** mode, use a PNG or uncompressed 32-bit BGRA BMP for per-pixel alpha, or a regular BMP for white-as-transparent artwork.
> - X4: Use a resolution of 480x800 pixels to match the device's screen resolution.
> - X3: Use a resolution of 528x792 pixels to match the device's screen resolution.

> [!TIP]
> You can set an image as the sleep screen cover directly from the BMP image viewer in the **[Browse Files](#33-browse-files-screen)** screen.

---

### 3.8 Custom Fonts (SD Card)

CrossPoint supports loading additional fonts from the SD card, extending beyond the two built-in families (Noto Serif, Noto Sans). Custom fonts can include extended Unicode coverage, enabling CJK (Chinese, Japanese, Korean) and other scripts.

There are three ways to install fonts:

1. **Download from device (recommended):** Go to **Settings -> Reader -> Manage Fonts**, browse the available font families, and select one to download over Wi-Fi.
2. **Upload via web interface:** While in **File Transfer** mode, open the web UI in a browser and navigate to the **Fonts** tab to upload `.cpfont` files.
3. **Manual SD card copy:** Download font files from the [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts) and copy them to `/.fonts/` (preferred) or `/fonts/` on your SD card.

Once installed, custom fonts appear in the **Font** tab of **Settings → Reader → Text Settings** alongside the built-in fonts.

See [docs/sd-card-fonts.md](./docs/sd-card-fonts.md) for full installation details and SD card folder structure.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning

| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Side Up**    |
| **Next Page**     | Press **Right** _or_ **Side Down** |

The role of the side buttons can be swapped in the **[Controls Settings](#363-controls)**.

If the **Short Power Button Click** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

### Chapter Navigation

Holding a page-turn button does nothing until you opt in: **Long-press button behavior** in the **[Controls Settings](#363-controls)** is **Off** by default, precisely so chapters cannot change by mistake. Set it to **Chapter skip** to get:

* **Next Chapter:** Press and **hold** the **Right** (or **Side Down**) button for about 0.7 seconds, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Side Up**) button the same way. From the middle of a chapter this jumps to the start of the current one first; a second hold moves to the previous chapter.

The same setting's other value, **Orientation change**, turns those holds into a rotation instead. That one is handled only by the EPUB reader.

A skip moves by chapter in EPUB books and by section in FB2 ones. In the paged formats that have no chapter structure — `.txt`, `.md` and `.xtc`/`.xtch` — it moves ten pages at a time.

A hold that came from the tilt sensor never counts as a long press, so tilting cannot trigger a skip.

### Auto Page Turn

Auto Page Turn automatically advances pages at a set interval, useful for hands-free reading. This feature can be enabled and configured from the **[Reader Menu](#5-reader-menu)** while reading an EPUB.

### Tilt Page Turn

On boards that carry a motion sensor, pages can be turned by tilting the device. The **Tilt Page Turn** row only appears in the Controls settings when the firmware finds that sensor at startup, which today means the **Xteink X3**, the **Xteink X4 Classic** and **Sticky**; the plain X4, the X4 Pro and Paper Mono have no IMU and never show the row. Values are **Off**, **Normal** and **Inverted**.

### Footnote Navigation

When an EPUB page carries footnote references or other internal links, there are three ways in:

* **Tap the marker.** On touch boards with **Touch Reader Controls** on, a tap on **any** link on the page — footnote markers included — is checked before the reader-menu and page-turn zones, and its box is padded slightly so a single superscript digit is still a reachable target. A page tracks up to 32 links.
* **The Reader Menu.** A **Footnotes** row appears in the **[Reader Menu](#5-reader-menu)** whenever the current page has footnote references, and lists them (up to 16 per page).
* **The Power button.** With **Short Power Button Click** set to "Footnotes", a click opens the page's only footnote directly, or the same list when there are several.

A short press of **Back** returns from a footnote to where you left off; up to three nested return positions are kept. With **Quick-return from footnotes** on (the default), a Power click also returns instead of opening another footnote.

If the device goes to sleep or you close the book while viewing a footnote, the book reopens to your original reading position, not the footnote.

### Dictionary Lookup

Words on the current page can be looked up in an offline StarDict dictionary stored on the SD card. Copy a dictionary to the `/dictionaries/` folder (a hidden `/.dictionaries/` is also scanned), select it in **Settings → Reader → Dictionary**, then start a lookup by choosing **Look Up** in the **[Reader Menu](#5-reader-menu)** (or by holding **Confirm**, if the **Long-press Menu** setting in **[Controls Settings](#363-controls)** is set to "Dictionary"). Use **Left/Right** to step between words (holding either repeats) and **Up/Down** to move between lines, then press **Confirm** — or tap the word — to show its definition. **Back** returns to the page. If no dictionary is selected, the reader shows "No dictionary set" for a couple of seconds instead of opening the selector.

See [docs/dictionary.md](docs/dictionary.md) for supported formats, setup, and where to find dictionaries.

### System Navigation

* **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
* **Return to Browse Files:** Press and hold the **Back** button for one second to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen. Turning on **Short Back to File Browser** in the **[Controls Settings](#363-controls)** swaps the two, so the short press goes to Browse Files and the hold goes Home.
* **Reader Menu:** Press **Confirm** to open the **[Reader Menu](#5-reader-menu)**, which includes chapter navigation, reading options, and more.
* **Long-press Confirm (configurable):** Holding **Confirm** runs the function chosen by the **Long-press Menu** setting in **[Controls Settings](#363-controls)**. It is **Disabled** by default, in which case the hold does nothing. "Bookmark" and "Dictionary" fire after 0.4 seconds and drop a bookmark or start a word lookup; "KOSync" needs a full second *and* saved sync credentials, and is simply not armed without them; "Reader Menu" exists only on boards with a Home key. On those boards a Home-key hold runs the same chosen function. A short press always opens the Reader Menu.

### Other Book Formats

`.epub` gets the full reading surface described above. The other formats open in their own readers, which share page turning, the status bar, progress saving and the Back navigation, but not the whole Reader Menu:

* **`.fb2`** *(fork-only)*: Confirm opens the same [Reader Menu](#5-reader-menu), without the Footnotes and Bookmarks rows. Select Chapter, Text Settings, Go to %, Go Home and Delete Book Cache work, as do the rows the menu handles by itself — Reading Orientation, Night Mode and Frontlight. The rest (Toggle Bookmark, Look Up, Auto Turn, Take screenshot, Show page as QR, Sync Progress) are EPUB-only and do nothing here — though the Power + Side Down screenshot combination still works. Long-press skip moves by section.
* **`.xtc` / `.xtch`**: Confirm goes straight to the chapter list — there is no menu in between. A file that carries no chapters has no list, so Confirm does nothing. These books are always rendered in portrait, whatever the Reading Orientation setting says, because XTC stores its pages as pre-rendered bitmaps rather than reflowable text.
* **`.txt` and `.md`**: Both open in the same plain-text reader; `.md` is read as plain text, with no Markdown formatting applied. There is no Reader Menu and no chapter list, so Confirm does nothing while reading.

### Supported Languages

CrossPoint renders text using the following Unicode character blocks, enabling support for a wide range of languages:

* **Latin Script (Basic, Latin-1 Supplement, Extended-A, plus selected Extended-B ranges):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, Catalan, and others.
* **Cyrillic Script (the U+0400–U+04FF block):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others. The Cyrillic Supplement and Extended blocks are not included.
* **Vietnamese:** Supported via extended Latin glyph coverage in the built-in reader fonts.

The UI includes Arabic and Hebrew (menus use built-in fonts with presentation-form coverage). Built-in **reader** fonts do not cover Chinese, Japanese, Korean, Arabic, Greek, Hebrew, or Farsi for book text. **CJK, Hebrew, Arabic, Greek, and other extended scripts can be enabled for reading by installing custom SD card fonts** — see [Custom Fonts (SD Card)](#38-custom-fonts-sd-card).

---

## 5. Reader Menu

Press **Confirm** while reading to open the Reader Menu. From here you can access reading utilities and navigation options without leaving the book. On touch boards it also opens with a downward swipe from the top edge — except on boards with a frontlight, where that swipe opens the frontlight panel instead — and, depending on **Show Reader Menu** in the [Controls settings](#363-controls), with a tap in the middle third of the page or an upward swipe from the bottom edge.

Touch boards can show it either as this full-screen list or as a toolbar drawn over the page; see **Reader Menu Style** in the [Reader settings](#362-reader). Button-only boards always get the list, whatever that setting says. In toolbar style the **More** sheet carries the same rows as the list, minus Select Chapter and Text Settings, which have their own toolbar buttons.

The list is headed by the book title and a line reading `Chapter: <page>/<pages> pages | Book: <percent>%`. Its rows always appear in this order. Footnotes, Bookmarks and Frontlight are the only conditional ones:

- **Select Chapter** – Open the table of contents to jump to a specific chapter (see [Chapter Selection](#51-chapter-selection) below).
- **Footnotes** – List the footnote references on the current page, up to 16 *(only shown when the current page has any)*.
- **Bookmarks** – Open this book's bookmark list *(only shown once the book has at least one)*, see [Bookmarks](#52-bookmarks) below.
- **Toggle Bookmark** – Bookmark the current page, or remove the bookmark if it is already bookmarked.
- **Text Settings** – Open the Text Settings screen (font, size, layout, style) without leaving the book.
- **Night Mode** – Invert the whole display. Selecting it flips its own On/Off value in place; the menu stays open.
- **Frontlight** – Turn the reading light on or off in place *(only on boards that have one)*.
- **Look Up** – Select a word on the current page and show its dictionary definition (see [docs/dictionary.md](docs/dictionary.md)). With no dictionary selected in **Settings → Reader → Dictionary** it briefly shows "No dictionary set" instead.
- **Reading Orientation** – Opens a picker over the menu: Portrait, Landscape CW, Inverted, Landscape CCW. The menu itself rotates at once so you can see the result; the page reflows when you close the menu.
- **Auto Turn (Pages Per Minute)** – Opens a picker over the menu: Off, 1, 3, 6 or 12 pages per minute.
- **Go to %** – Jump to a specific position in the book by percentage.
- **Take screenshot** – Save the current page as a BMP in `/screenshots/<book title>/`, named after the title with the chapter, page, percentage and a timestamp. Outside a book (and when the title has no usable characters) it lands in `/screenshots/` as `screenshot-<timestamp>.bmp`.
- **Show page as QR** – Encode the **text of the current page** into a QR code and show it full screen. It is the page's words, not a link or a reading position. A page longer than the format's 2,953-byte ceiling is truncated at a UTF-8 character boundary first, and the code's version is chosen from the payload length. Press Back, Confirm or tap to return to the menu.
- **Go Home** – Close the book and return to the Home screen.
- **Sync Progress** – Push or pull reading progress with a KOReader sync server (see [KOReader Sync Quick Setup](#367-koreader-sync-quick-setup)). With no credentials saved it just returns you to the page.
- **Delete Book Cache** – Clear the cached layout data for the current book and return to Home, so the book re-indexes on the next open. Your reading position is saved first and survives.

Only Reading Orientation and Auto Turn open a popup over the menu; Night Mode and Frontlight change their own value in place. Every other row either closes the menu or opens a screen of its own.

Press **Back** at any time to close the menu and return to your current page.

### 5.1 Chapter Selection

Accessible by selecting **Select Chapter** from the Reader Menu.

1. Use **Left** (or **Side Up**), or **Right** (or **Side Down**) to highlight the desired chapter.
2. Press **Confirm** to jump to that chapter.
3. *Alternatively, press **Back** to cancel and return to your current page.*

---

### 5.2 Bookmarks

Bookmarks can be created to quickly save and restore your place in a book.

Bookmarking is a **toggle**, not an add. Whichever way you trigger it, CrossPoint first looks for a bookmark already covering the current page: if it finds one it removes it and reports "Bookmark removed.", and only otherwise does it store a new one and report "Bookmark added.". Either popup disappears on its own after about 2.5 seconds.

There are two ways to trigger it:

- **Toggle Bookmark** in the **[Reader Menu](#5-reader-menu)**.
- Holding **Confirm** for about 0.4 seconds, but only after setting **Long-press Menu** to "Bookmark" in the **[Controls Settings](#363-controls)**. The setting is Disabled by default, so this shortcut is off until you turn it on. On boards with a Home key, a Home-key hold does the same.

A new bookmark records the position as a KOReader-style XPath plus a percentage, along with the chapter, page and page-count at the time and an offset into the chapter's visible text. It also keeps the first 72 characters of the page as a one-line summary for the list. Reopening it prefers the text offset, falls back to the saved page hints, and only then to the XPath and percentage — so a bookmark still lands on the right text after a font or margin change.

To open bookmarks, press **Confirm** while inside a book and select **Bookmarks**; the row only appears once the book has at least one. Highlight a bookmark and press **Confirm** to jump to it. To delete one, hold **Confirm** on it for about 0.7 seconds (or long-press it on a touch screen): a **Cancel / Delete** confirmation appears with **Cancel** preselected, so you must move to **Delete** and confirm. Dismissing the popup with **Back** cancels.

Bookmarks are stored per book as JSON under `/.crosspoint/bookmarks/`, in a file named after the book's path with the slashes flattened into underscores.

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following are **known limitations** of the shipped build:

* **Cover Images:** A large cover image embedded in an EPUB has to be extracted, decoded and rescaled the first time it is needed — once for the home screen thumbnail and once for the sleep screen. Each result is written to the book's cache folder and reused afterwards, but the first pass is slow enough that the home screen puts up a progress popup while it runs. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.
* **Unsupported Image Formats:** Only JPEG (`.jpg`, `.jpeg`) and PNG have decoders. Progressive JPEGs do render, but only their 1/8-scale preview is decoded and then smoothed back up, so they come out softer than a baseline JPEG. Anything else in an EPUB — GIF, SVG, WebP — has no decoder at all: the chapter is laid out with the tag's alt text as an italic `[Image: ...]` line instead, and an image with no alt text is dropped. An image whose format *is* supported but whose file will not decode (missing, empty or corrupt) keeps its reserved space and is drawn as an empty outlined box. The same `[Image: ...]` line is what the **Images** setting's "Placeholder" mode substitutes for *every* image.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Crosspoint, feel free to raise an issue ticket and attach the logs.

**Crash reports on SD card:** After a crash, CrossPoint automatically saves a crash report to `crash_report.txt` in the root of the SD card (no USB connection needed). On the next start it also boots into a **System Crash** screen showing the panic reason; press **Back** (or tap the screen) to continue to the normal interface. Include `crash_report.txt` with any bug report.

**Serial monitor logs:** For more detailed debugging, connect the device to a computer and run the custom debugging monitor script (requires Python 3 with `pyserial`, `colorama`, and `matplotlib`; install via `pip3 install pyserial colorama matplotlib`, and add `Pillow` if you want captured screenshots saved as BMP rather than raw data):

```
python3 scripts/debugging_monitor.py
```

The script auto-detects the serial port. You can also specify one explicitly:

```
python3 scripts/debugging_monitor.py /dev/ttyACM0        # Linux
python3 scripts/debugging_monitor.py /dev/tty.usbmodem1  # macOS
python3 scripts/debugging_monitor.py COM7                # Windows
```

**Features:**

- Color-coded log output by category (errors, memory, display, EPUB parsing, etc.)
- Live memory usage graph (free RAM, total RAM, max contiguous allocation) redrawn every second, fed by the heap statistics the firmware logs every 10 seconds while serial is connected
- Interactive command prompt — type a command at the `Command:` prompt and press Enter to send it to the device (the script prefixes it with `CMD:`)
- Screenshot capture — type `SCREENSHOT` at that prompt and the device sends back its raw framebuffer, which the script rotates and saves as `screenshot.bmp` in the current directory (or `screenshot.raw` if Pillow is not installed)

**Options:**

| Option               | Description                                               |
| -------------------- | --------------------------------------------------------- |
| `--baud RATE`        | Baud rate (default: 115200)                               |
| `--filter KEYWORD`   | Show only lines containing the keyword (case-insensitive) |
| `--suppress KEYWORD` | Hide lines containing the keyword (case-insensitive)      |

**Examples:**

```
# Show only memory-related log lines
python3 scripts/debugging_monitor.py --filter MEM

# Hide noisy SD card log lines
python3 scripts/debugging_monitor.py --suppress "[SD]"
```

Press **Ctrl-C** or close the graph window to exit.

If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot to the Home Screen. Holding **Back** during startup suppresses the automatic "reopen the last book" step, so a book that crashes the reader cannot keep restarting the device.

There can be issues with broken cache or config. In this case, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.json`, `state.json`, or `epub_*` cache directories in the `.crosspoint/` folder).

**Recovery firmware mode:** If the firmware is too broken to reach Settings, there is a second boot-time hold that flashes new firmware from the SD card. Copy a firmware `.bin` onto the card, then press and release the Reset button and hold the **side Up** button together with the **Power** button (on the X4 Pro and X4 Classic, hold the **side Down** button instead — their Up key doubles as a boot strap). The device starts directly in **Recovery Mode** with a file picker limited to `.bin` files; select one to validate and flash it. Cancelling simply reopens the picker, so you cannot slip out of recovery into a half-started interface. This mode is not available on Paper Mono.
