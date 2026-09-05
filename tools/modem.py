#!/usr/bin/env python3
"""Command line driver for sub-ghz-modem, speaking the binary TLV protocol.

  modem.py info
  modem.py set modem=lora freq=434.0 sf=9 power=14
  modem.py tx "hello world"
  modem.py tx --hex DEADBEEF
  modem.py cw 10
  modem.py listen --seconds 120
  modem.py sweep power -9,0,7,14,17,20,22 --gap 5
  modem.py ab reg 1,0 --gap 30 --cw 10
  modem.py preset meshtastic-eu --listen
  modem.py monitor          # decode every frame, for protocol debugging

Port is auto-detected from /dev/serial/by-id when --port is omitted and exactly
one candidate is present.
"""

import argparse
import glob
import os
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pip install pyserial")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto  # noqa: E402

PRESETS = {
    # Decoded off the air from a live node, not taken from the Meshtastic source.
    "meshtastic-eu": "modem=lora freq=869.502 bw=250 sf=11 cr=5 sync=0x2B preamble=16",
    "lora-eu-868": "modem=lora freq=868.1 bw=125 sf=9 cr=7 sync=0x12 preamble=8 power=14",
    "lora-434": "modem=lora freq=434.0 bw=125 sf=9 cr=7 sync=0x12 preamble=8 power=10",
    "fsk-50k": "modem=fsk bitrate=50 fdev=25 rxbw=156.2 shaping=0.5 syncbytes=2DD4",
    # Fine Offset WH24/WH25/WH51/WH65 family: 17.24 kbit/s FSK, AA AA AA preamble
    # then a 2D D4 sync, raw fixed-length frame, station's own CRC in the payload.
    "fineoffset-868": "modem=fsk freq=868.35 bitrate=17.241 fdev=35 rxbw=117.3 "
                      "shaping=none syncbytes=2DD4 fskcrc=0 fixedlen=20 fskpreamble=32",
    "fineoffset-433": "modem=fsk freq=433.92 bitrate=17.241 fdev=35 rxbw=117.3 "
                      "shaping=none syncbytes=2DD4 fskcrc=0 fixedlen=20 fskpreamble=32",
}


def find_port():
    cands = sorted(glob.glob("/dev/serial/by-id/*"))
    if len(cands) == 1:
        return os.path.realpath(cands[0])
    if not cands:
        sys.exit("no serial device found, pass --port")
    sys.exit("several serial devices, pass --port:\n  " + "\n  ".join(cands))


def parse_value(name, text):
    """Accept human spellings for the enum-ish parameters."""
    low = text.lower()
    if name == "modem":
        if low not in proto.MODEMS:
            sys.exit(f"modem must be one of {', '.join(sorted(proto.MODEMS))}")
        return proto.MODEMS[low]
    if name == "shaping":
        if low not in proto.SHAPING:
            sys.exit(f"shaping must be one of {', '.join(proto.SHAPING)}")
        return proto.SHAPING[low]
    if name == "reg":
        return 1 if low in ("ldo", "1", "true") else 0
    if name == "syncbytes":
        return text
    if name in ("crc", "fskcrc", "lrgrid"):
        return 1 if low in ("1", "on", "true", "yes", "narrow") else 0
    if low.startswith("0x"):
        return int(text, 16)
    return text


class Modem:
    def __init__(self, port, baud=115200, verbose=False):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.dec = proto.Decoder()
        self.verbose = verbose
        self.pending = []
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def _read(self):
        data = self.ser.read(4096)
        if data:
            self.pending.extend(self.dec.feed(data))

    def send(self, msg_type, value=b""):
        if self.verbose:
            print(f">> {proto.MSG_NAME.get(msg_type, msg_type)} {value.hex()}",
                  file=sys.stderr)
        self.ser.write(proto.frame(msg_type, value))
        self.ser.flush()

    def wait(self, want=None, timeout=3.0, show_events=True):
        """Collect frames until one of `want` arrives or the timeout expires."""
        end = time.time() + timeout
        while time.time() < end:
            self._read()
            while self.pending:
                mtype, value = self.pending.pop(0)
                if mtype in (proto.RX,) and show_events:
                    show(mtype, value)
                    continue
                if want is None or mtype in want:
                    return mtype, value
                show(mtype, value)
            time.sleep(0.01)
        return None, None

    def request(self, msg_type, value=b"", want=None, timeout=3.0):
        self.send(msg_type, value)
        mtype, val = self.wait(want or (proto.ACK, proto.ERR, proto.CONFIG,
                                        proto.INFO, proto.STATS,
                                        proto.DIAG_RESULT, proto.TX_DONE),
                               timeout)
        if mtype is None:
            sys.exit("no reply from modem")
        show(mtype, val)
        if mtype == proto.ERR:
            sys.exit(1)
        return mtype, val

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self._read()
            while self.pending:
                show(*self.pending.pop(0), stamp=True)
            time.sleep(0.02)


def show(mtype, value, stamp=False):
    prefix = f"{time.strftime('%H:%M:%S')}  " if stamp else ""
    if mtype == proto.ACK:
        print(f"{prefix}ok")
    elif mtype == proto.ERR:
        reason = value[0] if value else 0
        code = int.from_bytes(value[1:3], "little", signed=True) if len(value) >= 3 else 0
        print(f"{prefix}error: {proto.ERR_NAME.get(reason, reason)} (code {code})")
    elif mtype == proto.CONFIG:
        cfg = proto.decode_config(value)
        print(prefix + "  ".join(f"{k}={v}" for k, v in cfg.items()))
    elif mtype == proto.INFO:
        nfo = proto.decode_info(value)
        print(prefix + "  ".join(f"{k}={v}" for k, v in nfo.items()))
    elif mtype == proto.TX_DONE:
        length = int.from_bytes(value[0:2], "little")
        ms = int.from_bytes(value[2:6], "little")
        print(f"{prefix}sent {length} bytes in {ms} ms")
    elif mtype == proto.RX:
        r = proto.decode_rx(value)
        tag = "rx-crcfail" if r.get("crc_error") else "rx"
        body = r.get("payload", b"")
        print(f"{prefix}{tag} {len(body):>3}B  rssi {r['rssi']:>7.1f}  "
              f"snr {r['snr']:>6.1f}  {body!r}  {body.hex().upper()}")
    elif mtype == proto.STATS:
        tx, rx, err = (int.from_bytes(value[i:i + 4], "little") for i in (0, 4, 8))
        print(f"{prefix}tx={tx} rx={rx} err={err}")
    elif mtype == proto.DIAG_RESULT:
        ocp = value[0] if value else 0
        errs = int.from_bytes(value[1:3], "little") if len(value) >= 3 else 0
        flags = []
        if errs & 0x100:
            flags.append("PA_RAMP")
        if errs & 0x020:
            flags.append("XOSC_START")
        print(f"{prefix}ocp=0x{ocp:02X} ({ocp * 2.5:.1f} mA)  "
              f"errors=0x{errs:04X} {' '.join(flags) or 'none'}")
    elif mtype == proto.READY:
        print(f"{prefix}modem ready")
    else:
        print(f"{prefix}frame {proto.MSG_NAME.get(mtype, mtype)} {value.hex()}")


def set_params(m, pairs):
    value = b""
    for pair in pairs:
        if "=" not in pair:
            sys.exit(f"expected name=value, got {pair!r}")
        name, raw = pair.split("=", 1)
        name = name.lower()
        try:
            value += proto.encode_param(name, parse_value(name, raw))
        except KeyError as exc:
            sys.exit(str(exc))
    m.request(proto.SET_CONFIG, value, want=(proto.CONFIG, proto.ERR))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("-v", "--verbose", action="store_true")
    sub = ap.add_subparsers(dest="cmd", required=True)

    for name in ("ping", "info", "get", "stats", "save", "load", "reset", "diag"):
        sub.add_parser(name)

    p = sub.add_parser("set", help="apply name=value settings in one frame")
    p.add_argument("pairs", nargs="+", metavar="NAME=VALUE")

    p = sub.add_parser("tx")
    p.add_argument("payload")
    p.add_argument("--hex", action="store_true")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--gap", type=float, default=2.0)

    p = sub.add_parser("cw", help="unmodulated carrier")
    p.add_argument("seconds", type=int, nargs="?", default=10)

    p = sub.add_parser("listen")
    p.add_argument("--seconds", type=float, default=0)

    for name in ("sweep", "ab"):
        p = sub.add_parser(name, help="step one setting, transmitting at each value")
        p.add_argument("key")
        p.add_argument("values", nargs="?")
        p.add_argument("--values", dest="values_opt", help=argparse.SUPPRESS)
        p.add_argument("--gap", type=float, default=5.0 if name == "sweep" else 30.0)
        p.add_argument("--cw", type=int, default=0 if name == "sweep" else 10)
        p.add_argument("--payload", default="P" * 20)

    p = sub.add_parser("preset")
    p.add_argument("name", choices=sorted(PRESETS))
    p.add_argument("--listen", action="store_true")

    p = sub.add_parser("scan", help="RSSI sweep across a band, no SDR needed")
    p.add_argument("start", type=float, help="MHz")
    p.add_argument("stop", type=float, help="MHz")
    p.add_argument("--step", type=float, default=0.05, help="MHz")
    p.add_argument("--dwell", type=int, default=20, help="ms per step")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--threshold", type=float, default=6.0,
                   help="dB above the median floor to report")

    p = sub.add_parser("led", help="force LEDs on, for checking pin mapping")
    p.add_argument("colours", help="comma separated: red,green,blue,off,all")

    p = sub.add_parser("pin", help="drive any pin high or low, for finding LEDs")
    p.add_argument("pin", type=int)
    p.add_argument("level", type=int, choices=[0, 1])

    p = sub.add_parser("send", help="transmit a higher-level format")
    p.add_argument("kind", choices=sorted(proto.KINDS))
    p.add_argument("payload", nargs="?", default="")
    p.add_argument("--hex", action="store_true", help="payload is hex, not text")
    p.add_argument("--src", help="source callsign (aprs, ax25)")
    p.add_argument("--srcssid", type=int)
    p.add_argument("--dst", help="destination callsign (aprs, ax25)")
    p.add_argument("--dstssid", type=int)
    p.add_argument("--lat", help="APRS latitude, e.g. 4911.67N")
    p.add_argument("--lon", help="APRS longitude, e.g. 00610.90E")
    p.add_argument("--symbol", help="APRS symbol character")
    p.add_argument("--addr", type=int, help="POCSAG capcode")
    p.add_argument("--rate", type=int, help="baud, or words per minute for morse")
    p.add_argument("--shift", type=int, help="Hz, for rtty and fsk4")
    p.add_argument("--encoding", type=int)

    sub.add_parser("monitor", help="decode every frame until ctrl-c")

    # A value list like "-9,0,7" looks like a flag to argparse, and "--" would
    # swallow every later option. Rewrite such tokens into --values=... instead.
    # A value list like "-9,0,7" looks like a flag to argparse. Rewrite it, but
    # not when it is the argument of a preceding option such as --threshold.
    argv, prev = [], ""
    for t in sys.argv[1:]:
        neg = len(t) > 1 and t[0] == "-" and (t[1].isdigit() or t[1] == ".")
        argv.append(f"--values={t}" if neg and not prev.startswith("-") else t)
        prev = t
    a = ap.parse_args(argv)
    if getattr(a, "values_opt", None):
        a.values = a.values_opt

    m = Modem(a.port or find_port(), a.baud, a.verbose)

    simple = {"ping": proto.PING, "info": proto.GET_INFO, "get": proto.GET_CONFIG,
              "stats": proto.GET_STATS, "save": proto.SAVE, "load": proto.LOAD,
              "reset": proto.RESET, "diag": proto.DIAG}

    if a.cmd in simple:
        m.request(simple[a.cmd])

    elif a.cmd == "set":
        set_params(m, a.pairs)

    elif a.cmd == "tx":
        data = bytes.fromhex(a.payload) if a.hex else a.payload.encode()
        for i in range(a.repeat):
            print(time.strftime("%H:%M:%S"), end="  ")
            m.request(proto.TX, data, want=(proto.TX_DONE, proto.ERR),
                      timeout=max(8.0, a.gap))
            if i + 1 < a.repeat:
                time.sleep(a.gap)

    elif a.cmd == "cw":
        print(f"{time.strftime('%H:%M:%S')}  carrier for {a.seconds} s")
        m.send(proto.CW, a.seconds.to_bytes(2, "little"))
        m.wait((proto.TX_DONE, proto.ERR), timeout=a.seconds + 5)

    elif a.cmd == "listen":
        m.request(proto.RX_ENABLE, b"\x01")
        print("listening, ctrl-c to stop")
        try:
            m.pump(a.seconds or 10 ** 9)
        except KeyboardInterrupt:
            print()

    elif a.cmd in ("sweep", "ab"):
        if not a.values:
            sys.exit("give a comma separated value list")
        for raw in a.values.split(","):
            raw = raw.strip()
            set_params(m, [f"{a.key}={raw}"])
            print(f"{time.strftime('%H:%M:%S')}  {a.key}={raw}", end="  ")
            if a.cw:
                m.send(proto.CW, a.cw.to_bytes(2, "little"))
                m.wait((proto.TX_DONE, proto.ERR), timeout=a.cw + 5)
            else:
                m.request(proto.TX, a.payload.encode(),
                          want=(proto.TX_DONE, proto.ERR), timeout=8.0)
            time.sleep(a.gap)

    elif a.cmd == "preset":
        set_params(m, PRESETS[a.name].split())
        if a.listen:
            m.request(proto.RX_ENABLE, b"\x01")
            print("listening, ctrl-c to stop")
            try:
                m.pump(10 ** 9)
            except KeyboardInterrupt:
                print()

    elif a.cmd == "scan":
        import struct as _s
        for _ in range(a.repeat):
            vals, freqs = [], []
            # The firmware bounds a single request to 240 steps, so walk the
            # range in chunks and stitch the results.
            f0, f1, df = round(a.start * 1e6), round(a.stop * 1e6), round(a.step * 1e6)
            chunk_start = f0
            while chunk_start <= f1:
                chunk_stop = min(f1, chunk_start + 239 * df)
                m.send(proto.SCAN, _s.pack("<IIIH", chunk_start, chunk_stop, df, a.dwell))
                mtype, val = m.wait((proto.SCAN_RESULT, proto.ERR),
                                    timeout=60.0, show_events=False)
                if mtype != proto.SCAN_RESULT:
                    show(mtype or 0, val or b"")
                    sys.exit(1)
                start, step, chunk = proto.decode_scan(val)
                for i, v in enumerate(chunk):
                    freqs.append((start + i * step) / 1e6)
                    vals.append(v)
                chunk_start = chunk_stop + df

            ordered = sorted(vals)
            floor = ordered[len(ordered) // 2]
            peak = ordered[-1]
            print(f"{time.strftime('%H:%M:%S')}  {len(vals)} steps  "
                  f"median floor {floor:.1f} dBm  peak {peak:.1f} dBm")
            hits = [(f, v) for f, v in zip(freqs, vals) if v > floor + a.threshold]
            if not hits:
                print(f"  nothing more than {a.threshold} dB above the floor")
            for f, v in sorted(hits, key=lambda x: -x[1]):
                print(f"  {f:9.3f} MHz  {v:7.1f} dBm  +{v - floor:.1f} dB  "
                      f"{'#' * min(int(v - floor), 50)}")

    elif a.cmd == "led":
        bits = {"red": 1, "green": 2, "blue": 4, "all": 7, "off": 0}
        mask = 0
        for name in a.colours.split(","):
            if name.strip() not in bits:
                sys.exit(f"colours: {', '.join(bits)}")
            mask |= bits[name.strip()]
        m.request(proto.LED, bytes([mask]))

    elif a.cmd == "pin":
        m.request(proto.PIN, bytes([a.pin & 0xFF, a.level]))

    elif a.cmd == "send":
        body = bytes.fromhex(a.payload) if a.hex else a.payload.encode()
        fields = {"kind": proto.KINDS[a.kind], "text": body or None,
                  "src": a.src, "srcssid": a.srcssid, "dst": a.dst,
                  "dstssid": a.dstssid, "lat": a.lat, "lon": a.lon,
                  "symbol": a.symbol, "addr": a.addr, "rate": a.rate,
                  "shift": a.shift, "encoding": a.encoding}
        print(time.strftime("%H:%M:%S"), end="  ")
        m.request(proto.PROTO, proto.encode_proto(fields),
                  want=(proto.TX_DONE, proto.ERR), timeout=60.0)

    elif a.cmd == "monitor":
        print("monitoring, ctrl-c to stop")
        try:
            m.pump(10 ** 9)
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
