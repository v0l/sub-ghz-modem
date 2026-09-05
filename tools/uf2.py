#!/usr/bin/env python3
"""Convert a T-Echo build into a UF2 for drag-and-drop flashing.

  tools/uf2.py .pio/build/techo/firmware.zip .pio/build/techo/firmware.uf2

Serial DFU does not work on the T-Echo's 0.6.1 bootloader: it enumerates in DFU
mode and then answers nothing, so adafruit-nrfutil times out. Double-tap reset
for the TECHOBOOT drive and copy the UF2 onto it instead. Note that a 1200 bps
touch reaches serial DFU only, which has no mass storage.
"""

import struct
import sys
import zipfile

APP_BASE = 0x26000      # matches the s140 6.1.1 ldscript FLASH ORIGIN
FAMILY = 0xADA52840     # nRF52840
MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY = 0x00002000


def convert(src: str, dst: str) -> int:
    if src.endswith(".zip"):
        data = zipfile.ZipFile(src).read("firmware.bin")
    else:
        data = open(src, "rb").read()

    total = (len(data) + 255) // 256
    out = bytearray()
    for i in range(total):
        chunk = data[i * 256:(i + 1) * 256].ljust(256, b"\x00")
        out += struct.pack("<IIIIIIII", MAGIC0, MAGIC1, FLAG_FAMILY,
                           APP_BASE + i * 256, 256, i, total, FAMILY)
        out += chunk + b"\x00" * (512 - 32 - 256 - 4)
        out += struct.pack("<I", MAGIC_END)
    open(dst, "wb").write(out)
    return total


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    n = convert(sys.argv[1], sys.argv[2])
    print(f"{sys.argv[2]}: {n} blocks, app base 0x{APP_BASE:X}")
