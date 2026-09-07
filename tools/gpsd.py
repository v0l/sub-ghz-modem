"""Minimal gpsd server, fed by the NMEA sentences a modem streams over serial.

Speaks enough of the gpsd JSON protocol (VERSION, DEVICES, WATCH, TPV, SKY)
for cgps, gpspipe, chrony and the gps3/gpsd-py3 client libraries, plus the raw
NMEA passthrough that WATCH nmea=true asks for.

Not a replacement for gpsd: there is no device probing, no control socket, no
RTCM, and one device only.
"""

import json
import socket
import threading
import time

GPSD_PORT = 2947
PROTO_MAJOR, PROTO_MINOR = 3, 11
RELEASE = "sub-ghz-modem"


def _deg(raw: str, hemi: str) -> float:
    """ddmm.mmmm plus N/S/E/W into signed degrees."""
    dot = raw.find(".")
    if dot < 3:
        raise ValueError(raw)
    deg = int(raw[:dot - 2])
    minutes = float(raw[dot - 2:])
    val = deg + minutes / 60.0
    return -val if hemi in ("S", "W") else val


def _f(text: str):
    try:
        return float(text)
    except ValueError:
        return None


def checksum_ok(line: str) -> bool:
    star = line.rfind("*")
    if not line.startswith("$") or star < 0 or star + 3 > len(line):
        return False
    sum_ = 0
    for ch in line[1:star]:
        sum_ ^= ord(ch)
    try:
        return sum_ == int(line[star + 1:star + 3], 16)
    except ValueError:
        return False


class NmeaState:
    """Accumulates sentences into the TPV and SKY reports gpsd clients expect."""

    def __init__(self, device="/dev/modem-gps"):
        self.device = device
        self.mode = 0            # 1 no fix, 2 2D, 3 3D
        self.lat = self.lon = None
        self.alt = self.speed = self.track = None
        self.date = None         # ddmmyy from RMC
        self.time = None         # ISO 8601 UTC
        self.pdop = self.hdop = self.vdop = None
        self.used = 0
        self.sats = {}           # prn -> dict, refreshed by GSV
        self._gsv = {}

    def _stamp(self, hhmmss: str):
        if not self.date or len(hhmmss) < 6 or len(self.date) != 6:
            return None
        dd, mm, yy = self.date[0:2], self.date[2:4], int(self.date[4:6])
        year = 2000 + yy if yy < 80 else 1900 + yy
        frac = hhmmss[6:] if len(hhmmss) > 6 else ""
        return (f"{year:04d}-{mm}-{dd}T{hhmmss[0:2]}:{hhmmss[2:4]}:"
                f"{hhmmss[4:6]}{frac}Z")

    def feed(self, line: str):
        """Returns a list of report dicts to publish, possibly empty."""
        if not checksum_ok(line):
            return []
        body = line[1:line.rfind("*")]
        f = body.split(",")
        kind = f[0][2:] if len(f[0]) >= 5 else f[0]

        if kind == "RMC" and len(f) >= 10:
            self.date = f[9] or self.date
            self.time = self._stamp(f[1]) if f[1] else self.time
            if f[2] == "A" and f[3] and f[5]:
                self.lat = _deg(f[3], f[4])
                self.lon = _deg(f[5], f[6])
                if self.mode < 2:
                    self.mode = 2
            else:
                self.mode = 1
            knots = _f(f[7]) if len(f) > 7 else None
            self.speed = knots * 0.514444 if knots is not None else None
            self.track = _f(f[8]) if len(f) > 8 else None
            return [self.tpv()]

        if kind == "GGA" and len(f) >= 10:
            if f[6] not in ("", "0"):
                if f[2] and f[4]:
                    self.lat = _deg(f[2], f[3])
                    self.lon = _deg(f[4], f[5])
                self.alt = _f(f[9])
            self.used = int(f[7]) if f[7].isdigit() else self.used
            self.hdop = _f(f[8]) or self.hdop
            return []

        if kind == "GSA" and len(f) >= 18:
            self.mode = int(f[2]) if f[2].isdigit() else self.mode
            self.pdop, self.hdop, self.vdop = (_f(f[15]), _f(f[16]), _f(f[17]))
            used = {int(x) for x in f[3:15] if x.isdigit()}
            for prn, sat in self.sats.items():
                sat["used"] = prn in used
            return []

        if kind == "GSV" and len(f) >= 4:
            talker = f[0][:2]
            total, idx = f[1], f[2]
            if idx == "1":
                self._gsv[talker] = {}
            for i in range(4, len(f) - 3, 4):
                if not f[i].isdigit():
                    continue
                prn = int(f[i])
                self._gsv.setdefault(talker, {})[prn] = {
                    "PRN": prn, "el": _f(f[i + 1]), "az": _f(f[i + 2]),
                    "ss": _f(f[i + 3]) or 0.0,
                    "used": self.sats.get(prn, {}).get("used", False),
                }
            if total == idx:
                merged = {}
                for block in self._gsv.values():
                    merged.update(block)
                self.sats = merged
                return [self.sky()]
            return []

        return []

    def tpv(self) -> dict:
        r = {"class": "TPV", "device": self.device, "mode": self.mode}
        if self.time:
            r["time"] = self.time
        if self.mode >= 2 and self.lat is not None:
            r["lat"] = round(self.lat, 7)
            r["lon"] = round(self.lon, 7)
        if self.mode >= 3 and self.alt is not None:
            r["alt"] = r["altHAE"] = r["altMSL"] = self.alt
        for key, val in (("speed", self.speed), ("track", self.track)):
            if val is not None:
                r[key] = val
        return r

    def sky(self) -> dict:
        sats = [dict(s) for s in self.sats.values()]
        r = {"class": "SKY", "device": self.device,
             "nSat": len(sats), "uSat": sum(1 for s in sats if s["used"]),
             "satellites": sats}
        if self.time:
            r["time"] = self.time
        for key, val in (("pdop", self.pdop), ("hdop", self.hdop),
                         ("vdop", self.vdop)):
            if val is not None:
                r[key] = val
        return r


class _Client(threading.Thread):
    def __init__(self, conn, server):
        super().__init__(daemon=True)
        self.conn = conn
        self.server = server
        self.json = False
        self.nmea = False
        self.lock = threading.Lock()

    def push(self, text: str):
        try:
            with self.lock:
                self.conn.sendall(text.encode())
        except OSError:
            self.close()

    def send_obj(self, obj: dict):
        self.push(json.dumps(obj) + "\r\n")

    def close(self):
        try:
            self.conn.close()
        except OSError:
            pass
        self.server.drop(self)

    def run(self):
        self.send_obj({"class": "VERSION", "release": RELEASE, "rev": RELEASE,
                       "proto_major": PROTO_MAJOR, "proto_minor": PROTO_MINOR})
        buf = b""
        try:
            while True:
                data = self.conn.recv(4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self.command(line.decode("ascii", "replace").strip())
        except OSError:
            pass
        finally:
            self.close()

    def command(self, line: str):
        for cmd in [c for c in line.split(";") if c.strip()]:
            cmd = cmd.strip()
            if cmd.startswith("?WATCH"):
                if "=" in cmd:
                    try:
                        opts = json.loads(cmd.split("=", 1)[1])
                    except ValueError:
                        opts = {}
                    if opts.get("enable", True):
                        self.json = bool(opts.get("json", True))
                        self.nmea = bool(opts.get("nmea", False))
                    else:
                        self.json = self.nmea = False
                self.send_obj(self.server.watch(self))
                self.send_obj(self.server.devices())
            elif cmd.startswith("?DEVICES"):
                self.send_obj(self.server.devices())
            elif cmd.startswith("?DEVICE"):
                self.send_obj(self.server.device_obj())
            elif cmd.startswith("?POLL"):
                st = self.server.state
                self.send_obj({"class": "POLL", "time": st.time or "",
                               "active": 1, "tpv": [st.tpv()], "sky": [st.sky()]})
            elif cmd.startswith("?VERSION"):
                self.send_obj({"class": "VERSION", "release": RELEASE,
                               "rev": RELEASE, "proto_major": PROTO_MAJOR,
                               "proto_minor": PROTO_MINOR})
            else:
                self.send_obj({"class": "ERROR", "message": f"unknown command {cmd}"})


class GpsdServer:
    """Listens on the gpsd port; feed() publishes one NMEA sentence."""

    def __init__(self, host="127.0.0.1", port=GPSD_PORT, device="/dev/modem-gps"):
        self.state = NmeaState(device)
        self.device = device
        self.clients = []
        self.lock = threading.Lock()
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.listen(8)
        self.addr = self.sock.getsockname()
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            client = _Client(conn, self)
            with self.lock:
                self.clients.append(client)
            client.start()

    def drop(self, client):
        with self.lock:
            if client in self.clients:
                self.clients.remove(client)

    def device_obj(self) -> dict:
        return {"class": "DEVICE", "path": self.device, "driver": "NMEA0183",
                "activated": time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime()),
                "native": 0, "bps": 115200, "parity": "N", "stopbits": 1,
                "cycle": 1.0}

    def devices(self) -> dict:
        return {"class": "DEVICES", "devices": [self.device_obj()]}

    def watch(self, client) -> dict:
        return {"class": "WATCH", "enable": client.json or client.nmea,
                "json": client.json, "nmea": client.nmea, "raw": 0,
                "scaled": False, "timing": False, "split24": False, "pps": False}

    def feed(self, sentence: str):
        reports = self.state.feed(sentence)
        with self.lock:
            clients = list(self.clients)
        for c in clients:
            if c.nmea:
                c.push(sentence + "\r\n")
            if c.json:
                for r in reports:
                    c.send_obj(r)

    def client_count(self) -> int:
        with self.lock:
            return len(self.clients)
