#!/usr/bin/env python3
"""Flash or dump a RadioMaster RP2 over a plain serial adapter.

DTR and RTS reach nothing on this board, so esptool cannot put the ESP8285 into
download mode by itself: GPIO0 has to be low as power comes up, which is the
BOOT pad. What this does is watch the ROM's own boot banner, which prints at
74880 baud on every reset, and run esptool the moment it reports boot mode 1.

    tools/rp2flash.py write .pio/build/rp2-sx1281/firmware.bin
    tools/rp2flash.py read  rp2-stock.bin        # 2 MB, back this up first
    tools/rp2flash.py erase-write elrs.bin       # erase first, for ExpressLRS

Use erase-write when putting ExpressLRS back on: ExpressLRS issue #3023 has
RP2s bricking when one release is written over another without an erase.
"""

import subprocess
import sys
import time

import serial

FLASH_SIZE = "0x200000"


def wait_for_download_mode(port, seconds=45):
    s = serial.Serial(port, 74880, timeout=0.2)
    s.reset_input_buffer()
    print("hold BOOT to GND, apply power, release BOOT...", flush=True)
    seen = b""
    end = time.time() + seconds
    while time.time() < end:
        seen += s.read(512)
        if b"boot mode:(1" in seen:
            s.close()
            return True
        if b"boot mode:(3" in seen:
            s.close()
            print("booted the application: BOOT went high too early")
            return False
    s.close()
    print("no boot banner seen" if not seen else f"saw {seen[:80]!r}")
    return False


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("read", "write", "erase-write"):
        sys.exit(__doc__)
    action, path = sys.argv[1], sys.argv[2]
    port = sys.argv[3] if len(sys.argv) > 3 else "/dev/ttyUSB1"

    if not wait_for_download_mode(port):
        return 1

    base = ["esptool", "--port", port, "--baud", "115200",
            "--before", "no-reset", "--after", "no-reset"]
    # One invocation for erase and write: the stub only lives as long as the
    # esptool process, and nothing here can reset the chip to start another.
    if action == "read":
        cmd = base + ["read-flash", "0", FLASH_SIZE, path]
    elif action == "erase-write":
        cmd = base + ["write-flash", "--erase-all", "0", path]
    else:
        cmd = base + ["write-flash", "0", path]
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
