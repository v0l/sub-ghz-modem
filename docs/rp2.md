# RadioMaster RP2

An ExpressLRS receiver reused as a bench radio. It is an ESP8285 wired to an
SX1281, which matters because the SX128x is the only family that has the long
interleaved coding rates, `CR_LI_4_5`, `CR_LI_4_6` and `CR_LI_4_8`. Semtech
names them and describes nothing, no open decoder implements them, and
ExpressLRS 2.4 GHz codes every packet with one. A part that will encode
payloads you choose is the way to find out what they are.

The SPI bus is the ExpressLRS ESP8285 layout, from the firmware's own
`src/include/target/DIY_2400_RX_ESP8285_SX1280.h`:

| signal | GPIO |
|---|---|
| NSS | 15 |
| SCK | 14 |
| MOSI | 13 |
| MISO | 12 |
| BUSY | 5 |
| DIO1 | 4 |
| RST | 2 |

## Wiring

Four pads: 5V, GND, RX, TX, plus a BOOT pad. The ESP8285 is not 5 V tolerant,
so the adapter must be on 3.3 V logic. On most FTDI breakouts the jumper sets
VCC and the logic level together, and 3.3 V on the 5V pad browns out the
on-board regulator, so either use a board that exposes both rails or power the
receiver separately and share only ground.

`FTDI TX -> RP2 RX`, `FTDI RX -> RP2 TX`.

## Flashing

DTR and RTS go nowhere, so nothing can reset the chip for you. Hold BOOT to
ground, apply power, release BOOT. The ROM prints its banner at 74880 baud and
`boot mode:(1,x)` means download mode; `(3,x)` means it booted the application
and BOOT was high too early.

Back the stock image up first. It is the only copy that exists, and writing
this firmware removes the ExpressLRS bootloader along with the RX-pad trick
that ExpressLRS uses for UART flashing. After that, BOOT is the only way in.

`tools/rp2flash.py` waits for that banner and runs esptool as soon as it sees
download mode:

```sh
tools/rp2flash.py read rp2-stock.bin        # do this first, it is the only copy
pio run -e rp2-sx1281
tools/rp2flash.py write .pio/build/rp2-sx1281/firmware.bin
```

Restoring ExpressLRS is `tools/rp2flash.py write rp2-stock.bin`.
ExpressLRS issue #3023 reports RP2s bricking when an image is written over the
top of another, so erase the chip if a flash boots to nothing.

## The chip says SX1280

RadioLib chooses its driver by the version-string register at 0x01F0, and the
SX1281 on this board answers `SX1280` there despite the package marking. The
`SX1281` class compares that string and refuses the part with
`RADIOLIB_ERR_CHIP_NOT_FOUND` (-2), which looks exactly like a dead SPI bus, so
the environment builds `-DRADIO_SX1280`. The two classes differ only in the
ranging engine an SX1281 lacks and this modem never uses. When a begin() fails
here the firmware prints the sixteen identity bytes: all 0x00 or 0xFF is a bus
problem, anything legible is a naming one.

## ExpressLRS parameters

```sh
tools/modem.py --port /dev/ttyUSB1 preset elrs-2g4-150hz
tools/modem.py --port /dev/ttyUSB1 tx --hex 0000000000000000
```

`li=1` selects the long interleaved coding, `implicit=N` fixes the payload
length and takes the header off the air, and `crc=0` matches ExpressLRS, which
guards its packets with a CRC of its own instead. The two presets differ only
in payload length: 8 bytes is the ordinary packet, 13 the Full rates.

Transmit only, in the sense that matters: the SX1281 will receive, but a
receiver for a coding nobody has written down cannot decode what it hears. The
point of the board is that it encodes.
