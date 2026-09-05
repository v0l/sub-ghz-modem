# sub-ghz-modem

A sub-GHz radio you drive over a serial line. Hand it bytes, it transmits them;
it receives packets and hands them back with RSSI and SNR. No mesh, no routing,
no addressing. Whatever protocol you want lives on the host.

| board | MCU | radio | notes |
|---|---|---|---|
| LilyGO T-Beam v0.7 - v1.2 | ESP32 | SX1276 or SX1262 | AXP192/AXP2101 rails handled automatically, config in NVS |
| LilyGO T-Echo | nRF52840 | SX1262 | UF2/DFU flashing, config in LittleFS |
| ST NUCLEO-WL55JC | STM32WL55 | on-die SX126x core | no SPI radio bus, config in emulated EEPROM |

| modem | SX1276 | SX1262 / STM32WLx |
|---|---|---|
| LoRa | yes | yes |
| FSK, GFSK | yes | yes |
| OOK | yes | no, SX126x has no OOK receiver |
| LR-FHSS | no | transmit only, opt-in build |

GFSK is FSK with `shaping=0.5`, so there is no separate mode. RadioLib underneath,
and `STM32WLx` derives from `SX1262` there, so every board shares one protocol
implementation.

## Build

```sh
pio run -e tbeam-sx1276 -t upload     # T-Beam v0.7 / v1.0 / v1.1
pio run -e tbeam-sx1262 -t upload     # T-Beam v1.1-SX1262 / v1.2
pio run -e techo -t upload            # T-Echo
pio run -e wl55 -t upload             # NUCLEO-WL55JC1, 865-928 MHz
pio run -e wl55-lowband -t upload     # NUCLEO-WL55JC2, 430-510 MHz
```

The `-uart2` T-Beam environments move the protocol onto GPIO25/GPIO14 so USB
stays free for logs. `-lrfhss` environments add the LR-FHSS modem.

The repo ships `boards/t-echo.json` and a cut-down `variants/t-echo/`, so no
external variant package is required. Serial DFU does not work on the T-Echo's
0.6.1 bootloader: it enumerates in DFU mode and then answers nothing, so
`adafruit-nrfutil` times out, and a 1200 bps touch reaches only that serial mode.
Double-tap reset for the `TECHOBOOT` drive and copy a UF2 across instead:

```sh
pio run -e techo
tools/uf2.py .pio/build/techo/firmware.zip /media/$USER/TECHOBOOT/fw.uf2
```

For the WL55, `pio run -t upload` needs a udev rule for the ST-LINK
(`0483:374e`). Without one, copy `.pio/build/<env>/firmware.bin` onto the
board's `NOD_WL55JC` mass-storage drive instead, which needs no root.

## Protocol

Framed binary TLV. Full definition in `src/proto.h`, host side in
`tools/proto.py`.

```
A5 5A | TYPE u8 | LEN u16 | VALUE[LEN] | CRC16 u16
```

CRC16-CCITT (poly 0x1021, init 0xFFFF) over TYPE, LEN and VALUE. The magic gives
resync after a truncated write, and the length plus CRC means a corrupted frame
is dropped rather than half-executed. Config values are nested TLVs of
`ID u8 | LEN u8 | VALUE`. Everything is little endian, frequencies are in Hz and
rates in Hz or bit/s, so there are no floats on the wire.

Host to device: `PING GET_INFO GET_CONFIG SET_CONFIG TX CW RX_ENABLE SAVE LOAD
RESET GET_STATS DIAG SCAN LED PIN`.

Device to host: `ACK ERR INFO CONFIG TX_DONE RX STATS DIAG_RESULT READY
SCAN_RESULT`.

`SET_CONFIG` is atomic. The whole TLV list is applied to a scratch copy and the
radio reconfigured once; if any parameter is unknown or out of range the previous
config is restored and `ERR` names the offending id.

## Host CLI

```sh
tools/modem.py info
tools/modem.py set modem=lora freq=434.0 sf=9 power=22
tools/modem.py tx "hello" [--hex] [--repeat N --gap S]
tools/modem.py cw 10                       # unmodulated carrier
tools/modem.py listen [--seconds N]
tools/modem.py scan 433 435 --step 0.05    # RSSI sweep, no SDR needed
tools/modem.py sweep power -9,0,7,14,22 --gap 5
tools/modem.py preset lora-434 --listen
tools/modem.py diag                        # OCP and chip error flags
tools/modem.py monitor                     # decode every frame
```

The port auto-detects from `/dev/serial/by-id` when unambiguous. Since the link
is binary, a plain terminal is no longer useful; `monitor` replaces it.

## Parameters

| scope | names |
|---|---|
| all | `modem` `freq` MHz, `power` dBm, `reg` ldo/dcdc |
| LoRa | `bw` kHz, `sf` 6-12, `cr` 5-8, `sync`, `preamble`, `crc` |
| FSK, OOK | `bitrate` kbps, `fdev` kHz, `rxbw` kHz, `shaping`, `syncbytes`, `fskpreamble`, `fskcrc`, `fixedlen` |
| LR-FHSS | `lrbw` `lrcr` `lrgrid` |

`fixedlen` is for raw formats that carry no length byte, such as Fine Offset
sensors. Zero means variable length with a length byte.

## Display

Boards with a screen show firmware, board, radio, frequency and modem, so a
device on a shelf identifies itself. Drawn at boot and after a configuration
change only: an e-paper refresh takes seconds and blocks.

## LEDs

Red while the receiver is armed, blue while transmitting. On the WL55 those are
LED3/PB11 and LED1/PB15, active high, per UM2592. On the T-Echo they are two
dies of the RGB package, P0.13 and P0.14, active low.

## Gotchas worth knowing

These each cost real debugging time.

- **NUCLEO-WL55JC1 versus JC2.** Identical MCU and PlatformIO target; the sticker
  on the RF shield is the only way to tell. The JC1 is matched for 865-928 MHz,
  the JC2 for 430-510 MHz. Running a JC2 at 868 costs 30-40 dB in the matching
  network while the chip reports no error at all: registers correct, no PA ramp
  error, receive still usable. Build `wl55-lowband` for a JC2. Measured front-end
  response on a JC2 using `scan`: noise floor rises from -100 dBm at 288 MHz to
  -78 dBm at 360 and falls off a cliff above 486 MHz.
- **OCP clamps the high-power PA.** RadioLib's `SX126x::begin()` sets the
  over-current limit to 60 mA and `STM32WLx::setOutputPower()` deliberately
  preserves whatever it finds, so the HP amplifier is current-limited and
  radiates far below the requested power. This firmware sets 140 mA above
  14 dBm. Affects every SX126x build.
- **WL55 PA supply.** UM2592 6.6.3: the HP amplifier's REG PA must be fed
  directly from VDDSMPS, so `reg=dcdc` is required to reach 22 dBm.
- **TCXO voltage is per board**: 1.6 V T-Beam SX1262, 1.8 V T-Echo, 1.7 V WL55.
  Wrong value and the radio never leaves standby.
- **Never drive PA4-PA7 as GPIO on the WL55.** They are the `DEBUG_SUBGHZSPI`
  lines to the radio; doing so kills SPI and every command returns -707 until
  reset.
- **FSK preamble units differ**: bits on SX1262, bytes on SX1276. RadioLib passes
  it through as the chip defines it and this firmware does not normalise it.
- `rxbw` must be roughly `2 * fdev + bitrate` or the receiver misses packets.
- OOK on SX1276 tops out near 32.768 kbps and `fdev` is meaningless there.
- LR-FHSS is transmit only; enabling the receiver returns `ERR`.
- Changing any parameter re-runs `begin()` rather than poking registers. Simpler,
  and switching modulation needs it anyway.
- The stored config is a versioned struct blob. Bumping `CFG_VERSION` reverts a
  device to defaults rather than loading a mismatched layout.
- XPowersLib picks its chip with `#if/#elif`, so naming both `XPOWERS_CHIP_AXP192`
  and `XPOWERS_CHIP_AXP2101` silently compiles only the first and the other class
  is undeclared. Name neither and its `#else` branch defines all of them, which
  is what runtime probing needs.
- Toolchain rot: pinned platform versions disappear from the PlatformIO registry,
  and stm32duino 3.x moved to ArduinoCore-API whose typed `PinMode` breaks
  RadioLib's STM32WL HAL, so the WL55 build pins the 2.x core.

## Known issues

- `drainRx()` can re-read a stale buffer, emitting a spurious CRC-failed frame
  after a good packet.
- `transmit()` blocks, so the UART is not serviced during a long packet and the
  input FIFO can overrun. The frame CRC now catches the corruption rather than
  acting on it, but the frame is still lost.
- `batt_mv` on the T-Echo reads high; the divider constant needs checking
  against a meter. On the T-Beam it reads 0 with no battery fitted, which is the
  PMU reporting honestly rather than a fault.
- The T-Beam SX1262 variant and every LR-FHSS build are still untested on
  hardware. Tested: T-Beam v1.1 SX1276, T-Echo, Nucleo-WL55JC2.

## Regulatory

Set a frequency your region allows and mind duty cycle limits yourself: the
firmware enforces none. In the EU, 868.7-869.2 MHz is 25 mW at 0.1% duty, and
433.05-434.79 MHz is 10 mW ERP. Never transmit without an antenna.

## Existing alternatives

- **sh123/esp32_loraprs** for a KISS TNC over BT/BLE/USB/TCP with APRS iGate and digipeater.
- **Tasmota** with `USE_SPI_LORA` if the board already runs Tasmota. LoRa only.
- **Meshtastic** or **MeshCore** if you want a mesh rather than a raw link.
