#!/usr/bin/env python3
"""Capture the X4's USB-CDC serial log for N seconds. --reset pulses DTR/RTS first so the
boot banner (version) and a fresh [MEM] Min-Free appear. Reopens the port when the C3
re-enumerates. Usage: sercap.py OUT SECONDS [--reset]"""
import glob, sys, time
import serial
out, secs = sys.argv[1], float(sys.argv[2])
reset = '--reset' in sys.argv
def port():
    while True:
        p = glob.glob('/dev/cu.usbmodem*')
        if p: return p[0]
        time.sleep(1)
t0 = time.time()
with open(out, 'w') as f:
    ser = None
    first = True
    while time.time() - t0 < secs:
        try:
            if ser is None:
                ser = serial.Serial(port(), 115200, timeout=1)
                if first and reset:
                    ser.dtr = False; ser.rts = True; time.sleep(0.1)
                    ser.rts = False; time.sleep(0.1)
                    first = False
            line = ser.readline()
            if line:
                f.write('%8.3f %s\n' % (time.time() - t0, line.decode('utf-8', 'replace').rstrip()))
                f.flush()
        except (serial.SerialException, OSError):
            try: ser.close()
            except Exception: pass
            ser = None
            time.sleep(1)
