"""Binary TLV protocol for sub-ghz-modem. Mirrors src/proto.h.

Frame:  A5 5A  TYPE(u8)  LEN(u16 LE)  VALUE[LEN]  CRC16(u16 LE)
CRC16-CCITT, poly 0x1021, init 0xFFFF, over TYPE, LEN and VALUE.
"""

import struct

SOF = b"\xA5\x5A"
MAX_VALUE = 512

# Host to device
PING, GET_INFO, GET_CONFIG, SET_CONFIG = 0x01, 0x02, 0x03, 0x04
TX, CW, RX_ENABLE, SAVE, LOAD, RESET, GET_STATS, DIAG, SCAN, LED, PIN, PROTO = (
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10)

# Device to host
ACK, ERR, INFO, CONFIG, TX_DONE, RX, STATS, DIAG_RESULT, READY, SCAN_RESULT = (
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A)

MSG_NAME = {v: k for k, v in globals().items() if isinstance(v, int) and k.isupper()}

# Parameter ids: name -> (id, encoder, decoder). Values on the wire are integers
# in Hz or bit/s, so the CLI accepts human units and scales here.
P = {
    "modem":      (0x01, "u8"),
    "freq":       (0x02, "mhz"),
    "power":      (0x03, "i8"),
    "bw":         (0x04, "khz"),
    "sf":         (0x05, "u8"),
    "cr":         (0x06, "u8"),
    "sync":       (0x07, "u8"),
    "preamble":   (0x08, "u16"),
    "crc":        (0x09, "u8"),
    "bitrate":    (0x0A, "kbps"),
    "fdev":       (0x0B, "khz"),
    "rxbw":       (0x0C, "khz"),
    "shaping":    (0x0D, "u8"),
    "syncbytes":  (0x0E, "bytes"),
    "fskpreamble": (0x0F, "u16"),
    "fskcrc":     (0x10, "u8"),
    "reg":        (0x11, "u8"),
    "lrbw":       (0x12, "u8"),
    "lrcr":       (0x13, "u8"),
    "lrgrid":     (0x14, "u8"),
    "fixedlen":   (0x15, "u8"),
}
P_BY_ID = {v[0]: (k, v[1]) for k, v in P.items()}

INFO_FIELDS = {
    0x01: ("fw", "str"), 0x02: ("board", "str"), 0x03: ("radio", "str"),
    0x04: ("max_payload", "u16"), 0x05: ("batt_mv", "u16"),
    0x06: ("uptime_s", "u32"), 0x07: ("power_path", "str"),
}

MODEMS = {"lora": 0, "fsk": 1, "gfsk": 1, "ook": 2, "lrfhss": 3}
MODEM_NAME = {0: "LORA", 1: "FSK", 2: "OOK", 3: "LRFHSS"}
SHAPING = {"none": 0, "0.3": 1, "0.5": 2, "1.0": 3}
SHAPING_NAME = {0: "NONE", 1: "0.3", 2: "0.5", 3: "1.0"}

ERR_NAME = {
    1: "bad crc", 2: "bad length", 3: "unknown message", 4: "bad parameter",
    5: "out of range", 6: "unsupported on this radio", 7: "radio error",
    8: "transmit-only modem", 9: "busy transmitting",
}


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(msg_type: int, value: bytes = b"") -> bytes:
    body = struct.pack("<BH", msg_type, len(value)) + value
    return SOF + body + struct.pack("<H", crc16(body))


def encode_param(name: str, value) -> bytes:
    if name not in P:
        raise KeyError(f"unknown parameter {name!r}, known: {', '.join(sorted(P))}")
    pid, kind = P[name]
    if kind == "u8":
        raw = struct.pack("<B", int(value) & 0xFF)
    elif kind == "i8":
        raw = struct.pack("<b", int(value))
    elif kind == "u16":
        raw = struct.pack("<H", int(value))
    elif kind == "mhz":
        raw = struct.pack("<I", round(float(value) * 1_000_000))
    elif kind == "khz":
        raw = struct.pack("<I", round(float(value) * 1_000))
    elif kind == "kbps":
        raw = struct.pack("<I", round(float(value) * 1_000))
    elif kind == "bytes":
        raw = bytes.fromhex(value) if isinstance(value, str) else bytes(value)
    else:
        raise AssertionError(kind)
    return struct.pack("<BB", pid, len(raw)) + raw


def iter_tlv(value: bytes):
    pos = 0
    while pos + 2 <= len(value):
        pid, plen = value[pos], value[pos + 1]
        if pos + 2 + plen > len(value):
            return
        yield pid, value[pos + 2:pos + 2 + plen]
        pos += 2 + plen


def _int(raw: bytes) -> int:
    return int.from_bytes(raw, "little")


def decode_config(value: bytes) -> dict:
    out = {}
    for pid, raw in iter_tlv(value):
        if pid not in P_BY_ID:
            out[f"unknown_{pid:02X}"] = raw.hex()
            continue
        name, kind = P_BY_ID[pid]
        if kind == "mhz":
            out[name] = _int(raw) / 1_000_000
        elif kind in ("khz", "kbps"):
            out[name] = _int(raw) / 1_000
        elif kind == "i8":
            out[name] = struct.unpack("<b", raw)[0]
        elif kind == "bytes":
            out[name] = raw.hex().upper() or "off"
        else:
            out[name] = _int(raw)
    if "modem" in out:
        out["modem"] = MODEM_NAME.get(out["modem"], out["modem"])
    if "shaping" in out:
        out["shaping"] = SHAPING_NAME.get(out["shaping"], out["shaping"])
    if "reg" in out:
        out["reg"] = "LDO" if out["reg"] else "DCDC"
    return out


def decode_info(value: bytes) -> dict:
    out = {}
    for pid, raw in iter_tlv(value):
        if pid not in INFO_FIELDS:
            continue
        name, kind = INFO_FIELDS[pid]
        out[name] = raw.decode("utf-8", "replace") if kind == "str" else _int(raw)
    return out


def decode_rx(value: bytes) -> dict:
    if len(value) < 5:
        return {}
    rssi, snr, flags = struct.unpack("<hhB", value[:5])
    return {"rssi": rssi / 10.0, "snr": snr / 10.0,
            "crc_error": bool(flags & 0x01), "payload": value[5:]}


class Decoder:
    """Feed bytes, get (type, value) frames out. Resyncs on garbage."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        self.buf += data
        while True:
            start = self.buf.find(SOF)
            if start < 0:
                # Keep one byte in case a SOF straddles two reads.
                del self.buf[:max(0, len(self.buf) - 1)]
                return
            if start:
                del self.buf[:start]
            if len(self.buf) < 7:
                return
            msg_type, length = struct.unpack("<BH", self.buf[2:5])
            if length > MAX_VALUE:
                del self.buf[:2]
                continue
            total = 2 + 3 + length + 2
            if len(self.buf) < total:
                return
            body = bytes(self.buf[2:5 + length])
            got = struct.unpack("<H", self.buf[5 + length:total])[0]
            del self.buf[:total]
            if crc16(body) == got:
                yield msg_type, body[3:]


def decode_scan(value: bytes):
    """Returns (start_hz, step_hz, [rssi_dbm, ...])."""
    if len(value) < 10:
        return 0, 0, []
    start, step, steps = struct.unpack("<IIH", value[:10])
    vals = struct.unpack(f"<{steps}h", value[10:10 + steps * 2])
    return start, step, [v / 10.0 for v in vals]


# MSG_PROTO: higher-level on-air formats.
KINDS = {"aprs": 1, "ax25": 2, "pocsag": 3, "rtty": 4,
         "morse": 5, "hell": 6, "fsk4": 7}

K = {
    "kind": (0x01, "u8"), "text": (0x02, "str"), "addr": (0x03, "u32"),
    "rate": (0x04, "u16"), "shift": (0x05, "u32"), "src": (0x06, "str"),
    "srcssid": (0x07, "u8"), "dst": (0x08, "str"), "dstssid": (0x09, "u8"),
    "lat": (0x0A, "str"), "lon": (0x0B, "str"), "symbol": (0x0C, "char"),
    "encoding": (0x0D, "u8"),
}


def encode_proto(fields: dict) -> bytes:
    out = b""
    for name, value in fields.items():
        if value is None:
            continue
        kid, kind = K[name]
        if kind == "str":
            raw = value if isinstance(value, (bytes, bytearray)) else str(value).encode()
        elif kind == "char":
            raw = str(value)[:1].encode()
        elif kind == "u8":
            raw = struct.pack("<B", int(value))
        elif kind == "u16":
            raw = struct.pack("<H", int(value))
        else:
            raw = struct.pack("<I", int(value))
        out += struct.pack("<BB", kid, len(raw)) + raw
    return out
