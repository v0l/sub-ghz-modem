# sub-ghz-modem

A sub-GHz radio you drive over serial. Hand it bytes, it transmits them; it
receives packets and hands them back with RSSI and SNR. No mesh, no routing, no
addressing. Your protocol lives on the host.

| board | MCU | radio |
|---|---|---|
| LilyGO T-Beam v0.7 - v1.2 | ESP32 | SX1276 or SX1262 |
| LilyGO T-Echo | nRF52840 | SX1262 |
| ST NUCLEO-WL55JC | STM32WL55 | on-die SX126x |

LoRa and FSK/GFSK on every radio. OOK on SX1276 only. LR-FHSS on SX126x only,
transmit only, opt-in build.

## Build

```sh
pio run -e tbeam-sx1276 -t upload    # also tbeam-sx1262, and -uart2 variants
pio run -e wl55 -t upload            # JC1, 865-928 MHz
pio run -e wl55-lowband -t upload    # JC2, 430-510 MHz
pio run -e techo                     # then see docs/notes.md, UF2 only
```

## Use

```sh
tools/modem.py info
tools/modem.py set modem=lora freq=868.5 sf=9 power=17
tools/modem.py tx "hello"
tools/modem.py listen
tools/modem.py scan 433 435 --step 0.05   # RSSI sweep, no SDR needed
tools/modem.py cw 10                      # carrier, for spectrum work
```

`set` names: `modem freq power reg` always; `bw sf cr sync preamble crc` for
LoRa; `bitrate fdev rxbw shaping syncbytes fskpreamble fskcrc fixedlen` for
FSK/OOK; `lrbw lrcr lrgrid` for LR-FHSS. `--help` lists the rest.

## Protocol

Framed binary TLV, defined in `src/proto.h` and mirrored in `tools/proto.py`:

```
A5 5A | TYPE u8 | LEN u16 | VALUE[LEN] | CRC16 u16
```

CRC16-CCITT over type, length and value. Config values are nested
`ID u8 | LEN u8 | VALUE` TLVs. Little endian, frequencies in Hz, no floats on
the wire. `SET_CONFIG` is atomic: a bad parameter restores the previous config
and `ERR` names the offending id. Since the link is binary, `tools/modem.py
monitor` replaces a terminal.

## Before you file a bug

- **Nucleo JC1 vs JC2**: same MCU, different RF matching. A JC2 at 868 MHz loses
  30-40 dB while reporting no error at all. The shield sticker is the only way
  to tell. Build `wl55-lowband` for a JC2.
- **Region**: the firmware enforces no duty cycle or power limit. That is your
  problem. Never transmit without an antenna.
- Anything else surprising is probably in `docs/notes.md`, which records the
  traps that cost real debugging time.

## Known issues

- `batt_mv` on the T-Echo reads high; the divider constant is unverified.
- The T-Beam SX1262 variant and all LR-FHSS builds are untested on hardware.
  Tested: T-Beam v1.1 SX1276, T-Echo, Nucleo-WL55JC2.

## Alternatives

**sh123/esp32_loraprs** for a KISS TNC with APRS. **Tasmota** with
`USE_SPI_LORA`. **Meshtastic** or **MeshCore** for an actual mesh.
