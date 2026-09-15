# Contract: firmware images, release assets and update paths

## Release assets (GitHub `crosspoint-reader/crosspoint-reader`)

`GET https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest` parsed with a streaming JSON parser (tag ≤31 chars, URL ≤511, tokens ≤511 bytes, nesting ≤32; `size` must be an integer 0..2^32−1). The updater selects the asset named exactly `firmware.bin` for the combined ESP32-C3 X4/X3 build and `firmware-<board>.bin` otherwise (`sticky`, `x4pro`, `x4c`, `papermono`, `m5paper`, `lilygo`, `m5`, `murphy`, `delink`); the last duplicate wins. CI (`release.yml`) publishes `firmware.bin`, `firmware-sticky.bin`, `firmware-x4pro.bin`, `firmware-papermono.bin` (no `x4c` asset). Upstream develop has renamed assets to `crosspoint-<tag>-<device>.bin`, which this baseline does not recognise.

## Version comparison

Strip a leading `v`/`V`; both tag and running version must parse as `MAJOR.MINOR.PATCH`; compare per segment; equal numeric versions are "newer" only when the running build contains `-rc`. Non-semver tags are never newer.

## Board tag

Every image embeds exactly one `CROSSPOINT-BOARD-V1:<board>;` string in `.rodata`, derived from the `FREEINK_DEVICE_*` build flag (`x4pro`, `x4c`, `x4`, `papermono`, `sticky`, `m5paper`, `lilygo`, `m5`, `murphy`, `delink`; compile error otherwise). A scanner matches the magic byte-wise across chunks, captures ≤23 printable chars up to `;`, and latches a mismatch when the captured name differs from the running board. Untagged images are accepted.

## OTA path

`esp_ota_begin` on the next OTA partition → stream the asset; compare the u16 chip id at image offset 12 with the running partition's (skipped when unreadable); feed every byte to the board-tag scanner; abort (`esp_ota_abort`) on wrong chip/board (`WRONG_DEVICE_ERROR`), download or write failure; `esp_ota_end` verifies; `esp_ota_set_boot_partition` on success; Wi-Fi power save disabled during the transfer; progress per whole percent; restart after a 3 s "Update complete".

## SD path (and boot-time recovery)

Validation order: open; size ≥ 65536 (`TOO_SMALL`); ≤ next OTA partition size (`TOO_LARGE`); header magic 0xE9 (`BAD_MAGIC`); chip id (`BAD_CHIP`); each segment's 8-byte header and `data_len` within EOF (`BAD_SEGMENTS`); SHA-256 and XOR checksum (seed 0xEF) accumulated over segments in 4 KiB chunks; board tag (`WRONG_BOARD`); padded end + optional 32-byte SHA trailer must equal the file size and padding ≤16 (`BAD_SIZE`); stored checksum byte (`BAD_CHECKSUM`); trailer SHA (`BAD_SHA`). Flash: re-validate; erase 64 KiB ahead in 4 KiB sectors; write 4 KiB chunks with progress; then `ota_boot::switchTo(dest)` writes a fresh otadata entry {next sequence with parity matching the destination slot, label 0xFF×20, state NEW, CRC32-LE over `ota_seq`} into the inactive otadata sector (bypassing `esp_image_verify`; the eFuse block-revision check is neutralised by a linker wrap). Restart after 1.5 s. Recovery mode (Up, or Down on X4 Pro/X4 Classic, held at power-on; never on Paper Mono) launches this flow before Home and returns to the picker on every cancel or failure. Any `.bin` in any folder is selectable.

## Partition table (16 MB)

nvs 0x9000/0x5000; otadata 0xE000/0x2000; app0 (ota_0) 0x10000/0x640000; app1 (ota_1) 0x650000/0x640000; spiffs 0xC90000/0x360000 (unused); coredump 0xFF0000/0x10000.
