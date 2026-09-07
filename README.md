# sub-ghz-modem

A sub-GHz radio you drive over serial. Hand it bytes, it transmits them; it
receives packets and hands them back with RSSI and SNR. No mesh, no routing, no
addressing. Your protocol lives on the host.

| board | MCU | radio |
|---|---|---|
| LilyGO T-Beam v0.7 - v1.2 | ESP32 | SX1276 or SX1262 |
| LilyGO T-Echo | nRF52840 | SX1262 |
| ST NUCLEO-WL55JC | STM32WL55 | on-die SX126x |
| RadioMaster RP2 (an ExpressLRS receiver) | ESP8285 | SX1281, 2.4 GHz |

LoRa and FSK/GFSK on every radio. OOK on SX1276 only. LR-FHSS on SX126x only,
transmit only, opt-in build. The long interleaved coding rates are SX1281 only. Higher-level formats on top: APRS, AX.25, POCSAG,
RTTY, Morse, Hellschreiber and 4-FSK. Status screen on boards that have one.

## Build

```sh
pio run -e tbeam-sx1276 -t upload    # also tbeam-sx1262, and -uart2 variants
# RadioLib is pinned to a fork, see docs/notes.md for why
pio run -e wl55 -t upload            # JC1, 865-928 MHz
pio run -e wl55-lowband -t upload    # JC2, 430-510 MHz
pio run -e techo                     # then see docs/notes.md, UF2 only
pio run -e rp2-sx1281                # see docs/rp2.md, BOOT pad and no auto-reset
```

## Use

```sh
tools/modem.py info
tools/modem.py set modem=lora freq=868.5 sf=9 power=17
tools/modem.py tx "hello"
tools/modem.py listen
tools/modem.py scan 433 435 --step 0.05   # RSSI sweep, no SDR needed
tools/modem.py cw 10                      # carrier, for spectrum work

tools/modem.py send morse "DE N0CALL" --rate 25
tools/modem.py send pocsag --hex DEADBEEF --addr 123456
tools/modem.py send aprs "hi" --src N0CALL --lat 5336.00N --lon 00638.00W
tools/modem.py send rtty --hex 48454C4C4F --rate 45

tools/modem.py gps                        # NMEA from the onboard receiver
tools/modem.py gps --gpsd                 # and serve it to gpsd clients
tools/modem.py send aprs "hi" --src N0CALL --gps   # position from the fix
```

`send` kinds: `aprs ax25 pocsag rtty morse hell fsk4`. Payloads are bytes, so
`--hex` works everywhere except Morse and Hellschreiber, which are inherently
character formats. These take the radio into direct mode and restore your packet
config afterwards.

`set` names: `modem freq power reg` always; `bw sf cr sync preamble crc implicit
li` for LoRa; `bitrate fdev rxbw shaping syncbytes fskpreamble fskcrc fixedlen` for
FSK/OOK; `lrbw lrcr lrgrid` for LR-FHSS. `--help` lists the rest.

## GPS

The T-Beam and T-Echo carry a receiver; the Nucleo does not, and `info` reports
`gps=none` there. `gps on` streams each NMEA sentence to the host in its own
frame, and the setting survives `save`. Position tracking runs whether or not
the feed is on, so `send aprs --gps` fills latitude and longitude from the last
fix and fails with `no GPS fix` rather than transmitting a wrong position. A fix
older than 30 seconds counts as no fix.

`gps --gpsd` additionally serves the feed on 127.0.0.1:2947 in the gpsd JSON
protocol, so `cgps`, `gpspipe`, `chrony` and the usual client libraries can use
the modem as their GPS. It answers `?WATCH`, `?POLL`, `?DEVICES` and `?VERSION`,
emits `TPV` and `SKY`, and passes raw NMEA through when a client asks for it.
One device, no probing, no control socket: run the real gpsd if you need those.
Use `--gpsd-host 0.0.0.0` to share it on the network, which is unauthenticated.

## BLE

The same framed TLV protocol runs over a Nordic UART service, so a phone or any
host with a Bluetooth adapter can drive the modem without a cable. It is on by
default on the T-Echo and an opt-in env on the T-Beam (`tbeam-sx1276-ble`,
`tbeam-sx1262-ble`), where NimBLE costs about 400 kB of the app partition. The
Nucleo has no radio for it.

```sh
tools/modem.py list                       # serial ports and BLE modems in range
tools/modem.py --ble info                 # scans and connects
tools/modem.py --ble-target modem-FAC9 get
tools/modem.py ble off                    # stop advertising, over either link
```

The advert carries the Nordic UART service so generic BLE terminals work, and
the scan response carries `A55A0001-5A5A-4D4D-8D45-4D0000A55A5A`, our own
marker, so `list` can say which advertisers are actually sub-ghz-modems rather
than some other NUS device.

Needs `bleak` on the host. Each transport has its own frame parser, so two
clients cannot interleave into each other's frames, and a reply goes back to the
link its command came from. Unsolicited frames, received packets and NMEA, go to
every connected link.

Always disconnect cleanly: a host that exits without closing the link leaves
BlueZ holding the ACL, and the modem then stays connected and invisible to
everyone else until the stale connection is dropped. `modem.py` closes on exit,
including on ctrl-c.

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
- The GPS feed and the gpsd server work on a T-Echo, but only ever tested
  indoors: sentences, satellite lists, the gpsd `SKY` report and the no-fix
  refusal are all confirmed, an actual position is not.
- **Do not run the NMEA feed and a serial client at the same time as a BLE
  client.** Requests, config, scans and the GPS feed each work fine over BLE on
  their own, but with a BLE client connected and the feed running, the serial
  side stops getting replies and sentences arrive spliced. A full notification
  queue stalls `loop()` long enough to starve the UART; the write budget in
  `src/ble_nrf52.cpp` bounds it but does not fix it. It needs an outgoing queue
  drained from the loop rather than blocking writes.
- The T-Echo runs its BLE stack from the internal RC oscillator. On the crystal
  (`techo-lfxo`) the board advertises normally and then drops every connection
  inside a second with `le-connection-abort-by-local`.
- With two Bluetooth controllers present, scanning and connecting must use the
  same one, or every connection times out. `tools/bleuart.py` tries each in
  turn; `--ble-adapter hciN` pins it.
- The T-Beam SX1262 variant and all LR-FHSS builds are untested on hardware.
  Tested: T-Beam v1.1 SX1276, T-Echo, Nucleo-WL55JC2.
- APRS here is an AX.25 frame inside an ordinary FSK packet, not the 1200 baud
  AFSK a normal APRS station listens for, so normal APRS gear will not hear it.
- SX127x FSK payloads cap at 63 bytes, a FIFO limit, not a choice.
- APRS and AX.25 are verified: a position report transmitted T-Beam to T-Echo
  decodes to the right callsigns, SSIDs, control, PID and info with a valid
  CRC-16/X.25 FCS. The other five `send` formats key the radio with plausible
  timing but have not been decoded by an independent tool.
- On SX127x an AX.25 frame must fit the 63 byte FSK payload, which leaves about
  20 characters of APRS comment. Longer frames are now refused rather than
  transmitted truncated.

## Alternatives

**sh123/esp32_loraprs** for a KISS TNC with APRS. **Tasmota** with
`USE_SPI_LORA`. **Meshtastic** or **MeshCore** for an actual mesh.
