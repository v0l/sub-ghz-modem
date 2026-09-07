"""BLE transport for modem.py, over the Nordic UART service.

Needs bleak. The firmware advertises as modem-XXXX with the NUS service UUID,
so discovery does not depend on the name alone.

The event loop runs in its own thread and the byte queues are handed across, so
the rest of modem.py can keep treating the link as a blocking file.
"""

import asyncio
import os
import threading
import time

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # device receives
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # device notifies

# Advertised in the scan response by our firmware only, so a scan can tell a
# sub-ghz-modem from any other Nordic UART device.
MODEM_SERVICE = "a55a0001-5a5a-4d4d-8d45-4d0000a55a5a"

DEFAULT_MTU_PAYLOAD = 20   # ATT default, until the link negotiates something larger


class BleLink:
    """Same read/write/close surface as a pyserial port, over GATT."""

    def __init__(self, target=None, timeout=15.0, verbose=False, adapter=None):
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError:
            raise SystemExit("pip install bleak, or run under "
                             "'uv run --with bleak --with pyserial'")
        self._BleakClient = BleakClient
        self._BleakScanner = BleakScanner
        self.verbose = verbose
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.client = None
        self.name = None
        self.adapter = None
        # Scanning and connecting must use the same controller: with two
        # adapters present, letting BlueZ pick per call gives a device found on
        # one and a connection attempt on the other, which simply times out.
        self.adapters = [adapter] if adapter else self._adapters()

        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self._run_loop, daemon=True).start()
        self._call(self._connect(target, timeout), timeout + 80)

    @staticmethod
    def _adapters():
        try:
            found = sorted(os.listdir("/sys/class/bluetooth"))
        except OSError:
            found = []
        return found or [None]

    def _run_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def _call(self, coro, timeout):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout)

    async def _discover(self, target, timeout, adapter):
        kw = {"adapter": adapter} if adapter else {}
        found = await self._BleakScanner.discover(timeout=timeout,
                                                  return_adv=True, **kw)
        hits = []
        for dev, adv in found.values():
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            named = target and target.lower() in ((dev.name or "").lower(),
                                                  dev.address.lower())
            if named or (not target and (MODEM_SERVICE in uuids
                                         or NUS_SERVICE in uuids)):
                hits.append(dev)
        if not hits:
            return None
        if len(hits) > 1 and not target:
            listing = "\n  ".join(f"{d.address}  {d.name}" for d in hits)
            raise SystemExit(f"several BLE devices, pass --ble NAME|ADDRESS:\n  {listing}")
        return hits[0]

    def _on_notify(self, _handle, data):
        with self.lock:
            self.buf += data

    async def _connect(self, target, timeout):
        last = None
        for adapter in self.adapters:
            dev = await self._discover(target, min(timeout, 10.0), adapter)
            if not dev:
                continue
            self.name = dev.name or dev.address
            self.adapter = adapter
            kw = {"adapter": adapter} if adapter else {}
            # BlueZ drops the odd LE connection during service discovery; a
            # retry costs a second and saves a spurious failure.
            for _ in range(2):
                try:
                    self.client = self._BleakClient(dev, timeout=20.0, **kw)
                    await self.client.connect()
                    await self.client.start_notify(NUS_TX, self._on_notify)
                    return
                except Exception as exc:
                    last = exc
                    await asyncio.sleep(1.0)
        if last:
            raise SystemExit(f"BLE connect to {self.name} failed: {last}")
        raise SystemExit("no BLE modem found; is it advertising, and is "
                         "Bluetooth up?")

    @property
    def chunk(self):
        mtu = getattr(self.client, "mtu_size", 0) or 0
        return max(DEFAULT_MTU_PAYLOAD, mtu - 3)

    def read(self, n=4096):
        # Match pyserial's short read with a timeout rather than blocking.
        end = time.time() + 0.05
        while True:
            with self.lock:
                if self.buf:
                    out = bytes(self.buf[:n])
                    del self.buf[:len(out)]
                    return out
            if time.time() >= end:
                return b""
            time.sleep(0.005)

    def write(self, data: bytes):
        for off in range(0, len(data), self.chunk):
            self._call(self.client.write_gatt_char(
                NUS_RX, data[off:off + self.chunk], response=False), 10.0)
        return len(data)

    def flush(self):
        pass

    def reset_input_buffer(self):
        with self.lock:
            self.buf.clear()

    def close(self):
        try:
            self._call(self.client.disconnect(), 5.0)
        except Exception:
            pass
        self.loop.call_soon_threadsafe(self.loop.stop)


def scan(timeout=6.0, adapter=None):
    """Returns [(address, name, rssi, is_modem)] for every NUS-ish advertiser."""
    try:
        from bleak import BleakScanner
    except ImportError:
        raise SystemExit("pip install bleak, or run under "
                         "'uv run --with bleak --with pyserial'")

    async def go():
        kw = {"adapter": adapter} if adapter else {}
        found = await BleakScanner.discover(timeout=timeout, return_adv=True, **kw)
        out = []
        for dev, adv in found.values():
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            if MODEM_SERVICE not in uuids and NUS_SERVICE not in uuids:
                continue
            out.append((dev.address, dev.name or "?", adv.rssi,
                        MODEM_SERVICE in uuids))
        return sorted(out, key=lambda r: -r[2])

    return asyncio.run(go())
