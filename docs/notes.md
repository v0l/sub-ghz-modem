# Notes

Traps that cost real debugging time, and the reasoning behind choices that look
arbitrary in the code.

## Nucleo-WL55JC1 versus JC2

Identical MCU, identical PlatformIO target, different RF matching network. The
JC1 is matched for 865-928 MHz, the JC2 for 430-510 MHz, and the sticker on the
RF shield is the only way to tell them apart.

A JC2 transmitting at 868 MHz loses 30-40 dB in the matching network while every
diagnostic says the radio is fine: registers correct, no PA ramp error, OCP
tracking the requested power, receive still good enough to decode a strong local
node. Measured front-end response of a JC2 using `tools/modem.py scan`, noise
floor per frequency:

```
288 MHz  -100 dBm      360 MHz   -78 dBm  (peak)
486 MHz   -95 dBm      560 MHz  -103 dBm
```

Rising to a peak around 360-450 and falling off a cliff above 486 is the
signature of the low-band front end.

## SX126x over-current protection clamps the high-power PA

`SX126x::begin()` sets the over-current limit to 60 mA, and
`STM32WLx::setOutputPower()` deliberately reads OCP before configuring the PA and
writes it back unchanged, so it never rises. The high-power amplifier needs about
140 mA at 22 dBm, so it runs current-limited and radiates well below the
requested power, with no error anywhere.

This firmware sets 140 mA above 14 dBm and 60 mA below. Affects every SX126x
build, not just the STM32WL.

## WL55 PA supply

UM2592 6.6.3: the high-power amplifier's REG PA must be fed directly from
VDDSMPS, while the low-power one runs from the regulated VFBSMPS rail. So
`reg=dcdc` is required to reach 22 dBm, which is why that is the default.

## WL55 RF switch

`{PC3, PC4, PC5}` maps to `{FE_CTRL3, FE_CTRL1, FE_CTRL2}`. Verified against both
UM2592 and the Nucleo Rust BSP:

| mode | FE_CTRL1 | FE_CTRL2 | FE_CTRL3 |
|---|---|---|---|
| RX | high | low | high |
| TX low power | high | high | high |
| TX high power | low | high | high |

FE_CTRL3 is high in every mode because SB18 feeds the switch's own VDD from it.
Other STM32WL boards wire the front end differently and some populate only one
PA path; a wrong table gives a radio that initialises cleanly and then neither
transmits nor hears.

## Never drive PA4-PA7 as GPIO on the WL55

They are the `DEBUG_SUBGHZSPI` lines to the radio. Driving them kills SPI to the
transceiver and every command returns -707 until reset. Learned by sweeping pins
looking for an LED.

## TCXO voltage is per board

1.6 V on the T-Beam SX1262, 1.8 V on the T-Echo, 1.7 V on the WL55. Wrong value
and the radio never leaves standby.

## T-Echo flashing

Serial DFU does not work on the 0.6.1 bootloader: it enumerates in DFU mode
(`239a:002a`) and then answers nothing, so `adafruit-nrfutil` times out at the
first packet. A 1200 bps touch reaches only that same serial mode, which has no
mass storage. Double-tap reset for the `TECHOBOOT` drive, then:

```sh
pio run -e techo
tools/uf2.py .pio/build/techo/firmware.zip /media/$USER/TECHOBOOT/fw.uf2
```

The bootloader reports S140 6.1.1, so the application base is 0x26000.

## T-Echo LEDs

Meshtastic's variant comments disagree with its own `LED_RED`/`LED_BLUE`
defines. The defines are correct: red P0.13, blue P0.14, green P0.15, one RGB
package, active low. On the WL55 the user LEDs are LED1 blue PB15, LED2 green
PB9, LED3 red PB11, active high. LED4, LED5 and LED6 belong to the ST-LINK and
the power supply and cannot be driven by the MCU at all, LED6 being the bicolour
COM status light.

## XPowersLib picks a chip with #if/#elif

Defining both `XPOWERS_CHIP_AXP192` and `XPOWERS_CHIP_AXP2101` silently compiles
only the first, and the other class is undeclared. Define neither and its `#else`
branch defines all of them, which is what runtime probing needs.

## FSK details

- Preamble length is in bits on SX126x and bytes on SX127x. RadioLib passes it
  through as the chip defines it and this firmware does not normalise it.
- `rxbw` must be roughly `2 * fdev + bitrate` or the receiver misses packets.
- OOK on SX1276 tops out near 32.768 kbps and `fdev` is meaningless there.
- `fixedlen` is for raw formats with no length byte, such as Fine Offset
  sensors. A two-byte sync word false-triggers on noise; include a preamble byte
  or two, for example `AAAA2DD4`.

## Protocol clients need FSK and direct mode

RadioLib's AX25, APRS, Pager, RTTY, Morse, Hellschreiber and FSK4 clients all
drive the radio in direct mode, which the SX126x only offers from FSK. Calling
one while in LoRa returns -20 `RADIOLIB_ERR_WRONG_MODEM`. The firmware switches
to FSK for the duration and restores the previous config afterwards.

`MorseClient::standby()` and `HellClient::standby()` are private, so stop the
carrier through the PhysicalLayer instead.

## SX127x FSK payloads cap at 63 bytes

`variablePacketLengthMode(255)` returns -4 `RADIOLIB_ERR_PACKET_TOO_LONG` on
SX127x, whose FSK FIFO is 64 bytes, and that failure takes the whole radio init
down. Easy to miss because SX126x accepts 255 happily, so FSK looks fine until
you try it on a T-Beam.

## FSK deviation and receive bandwidth must match

FSK between a T-Beam SX1276 and a T-Echo SX1262 received nothing in either
direction while both sides reported successful transmits. Not a chip
incompatibility: the old defaults were `bitrate 4.8, fdev 5.0, rxbw 156.2`, which
puts about 15 kHz of signal in 156 kHz of receiver bandwidth, so the demodulator
saw roughly ten times more noise than signal.

`fdev 25, rxbw 58.6` works first time, both directions. Defaults changed to
match. The rule is the obvious one, `rxbw` about `2 * fdev + bitrate`, and it
bites hard rather than degrading gracefully.

Preamble units are still a real difference worth knowing (bits on SX126x, bytes
on SX127x) but were not the cause here.

## AX.25 is correct, and the bit order will fool you

RadioLib's AX.25 output is spec conformant: addresses shifted left one bit with
the HDLC extension bits, UI control 0x03, PID 0xF0, low-order bit first on air,
and a CRC-16/X.25 FCS transmitted low byte first. Verified over the air by
decoding a position report received on another board.

Two traps when writing a decoder for it. The radio hands you bytes MSB first,
but AX.25 bit order is LSB first, so unpack MSB first and repack LSB first.
And 0x7E is a palindrome, so the flags decode identically under either bit
order and will happily confirm a wrong assumption while the address field comes
out as garbage.

RadioLib is pinned to a fork branch carrying one fix: `AX25Client::sendFrame()`
used `preambleLen + 1` as the NRZI start index, but that value counts bytes
everywhere else in the function, so only the first nine bits were left
unencoded. AX.25 NRZI-encodes the whole transmission including flags.

## Design choices

- Changing any parameter re-runs `begin()` rather than poking registers. Simpler,
  and switching modulation needs a full re-init anyway.
- Transmit is non-blocking. A SF12 packet is 2.5 s of airtime, and a blocking
  `transmit()` left the UART unserviced long enough to overrun the input FIFO and
  merge two commands into one corrupt frame.
- Stored config is a versioned struct blob. Bumping `CFG_VERSION` reverts a
  device to defaults rather than loading a mismatched layout.

## Toolchain rot

Pinned platform versions disappear from the PlatformIO registry, so
`nordicnrf52@10.5.0` and `ststm32@17.6.0` both had to be bumped. stm32duino 3.x
moved to ArduinoCore-API, whose typed `PinMode` and `PinStatus` break RadioLib's
STM32WL HAL, so the WL55 build pins the 2.x core. RadioLib includes `SubGhz.h`
from its own sources and PlatformIO does not pass a sibling library's include
path into a dependency's build, hence the explicit `-I` and `lib_ldf_mode`.

Also: `%f` is absent from newlib-nano unless you link `-u _printf_float`, which
is why the ARM builds carry that flag and why the wire protocol has no floats.
