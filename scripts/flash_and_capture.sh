#!/usr/bin/env bash
# flash_and_capture.sh TREE ENV OUT SECS
# Upload ENV from the PlatformIO project at TREE to the X4 over USB-CDC, then capture SECS seconds
# of serial into OUT, resetting the board *under* capture so the boot lines are not lost.
# Safety: any running sercap.py is stopped first (a capture holding the port makes esptool fail and
# can leave the C3 in download mode), and the port is waited for before and after the flash
# because the C3 re-enumerates for a few seconds. Needs pyserial (python3 -m pip install pyserial).
set -u
TREE=$1; ENV=$2; OUT=$3; SECS=$4
HERE=$(cd "$(dirname "$0")" && pwd)
pkill -f "sercap.py" 2>/dev/null; sleep 1
until ls /dev/cu.usbmodem* >/dev/null 2>&1; do sleep 2; done
PORT=$(ls /dev/cu.usbmodem* | head -1)
( cd "$TREE" && pio run -e "$ENV" -t upload --upload-port "$PORT" ) > "$OUT.upload.log" 2>&1
rc=$?
echo "upload exit=$rc $(grep -E 'Hard resetting|SUCCESS|FAILED|error' "$OUT.upload.log" | tail -2 | tr '\n' ' ')"
[ $rc -ne 0 ] && exit $rc
sleep 3
until ls /dev/cu.usbmodem* >/dev/null 2>&1; do sleep 2; done
python3 "$HERE/sercap.py" "$OUT" "$SECS" --reset
echo "captured $(wc -l < "$OUT") lines"
grep -E '\[BENCH\]|\[MEM\]|Guru|abort|Backtrace|rst:' "$OUT" | cut -c1-160
