#include "ble_uart.h"

#if HAS_BLE && defined(BOARD_TECHO)

#include <bluefruit.h>

static BLEUart bleuart;
static bool enabled = false;
static bool stackUp = false;
static char devName[20];

// Bluefruit.connHandle() is only meaningful inside its own event callbacks, so
// asking it from loop() reports no connection at all. Track the handle here.
static volatile uint16_t connHdl = BLE_CONN_HANDLE_INVALID;

// BLE_MODEM_SERVICE, little endian on the wire as every 128-bit UUID is.
static const uint8_t modemUuid[16] = {
    0x5A, 0x5A, 0xA5, 0x00, 0x00, 0x4D, 0x45, 0x8D,
    0x4D, 0x4D, 0x5A, 0x5A, 0x01, 0x00, 0x5A, 0xA5,
};

static void onConnect(uint16_t handle) { connHdl = handle; }
static void onDisconnect(uint16_t, uint8_t) { connHdl = BLE_CONN_HANDLE_INVALID; }

void bleInit()
{
    // FICR device id, so two modems on one bench advertise different names.
    snprintf(devName, sizeof(devName), "modem-%04X",
             (unsigned)(NRF_FICR->DEVICEID[1] & 0xFFFF));

    // Bluefruit defaults to a 23 byte ATT MTU and a one-deep notification
    // queue, so a reply leaves at 20 bytes per connection event. An INFO or
    // CONFIG frame needs six of those, the write loop below gives up after
    // 25 ms, and the host sees a truncated frame and reports no reply.
    // BANDWIDTH_MAX asks the SoftDevice for MTU 247 and a three-deep queue,
    // which puts any frame the modem sends into one or two notifications.
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    stackUp = Bluefruit.begin();
    if (!stackUp) return;   // SoftDevice wanted more RAM than the linker gave

    Bluefruit.setName(devName);
    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);
    // 11.25-30 ms: BlueZ otherwise settles on 50 ms, which triples the
    // round trip of every request.
    Bluefruit.Periph.setConnInterval(9, 24);
    Bluefruit.setTxPower(4);
    bleuart.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleuart);
    // A second 128-bit UUID does not fit beside the first in a 31 byte advert,
    // so the marker rides in the scan response with the name. BlueZ merges the
    // two, and bleak reports both UUIDs.
    Bluefruit.ScanResponse.addUuid(BLEUuid(modemUuid));
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);   // units of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);
}

void bleEnable(bool on)
{
    if (!stackUp) return;
    enabled = on;
    if (on) {
        Bluefruit.Advertising.start(0);   // no timeout, advertise until connected
    } else {
        Bluefruit.Advertising.stop();
        if (Bluefruit.connected()) Bluefruit.disconnect(0);
    }
}

bool bleEnabled()   { return enabled; }
bool bleConnected() { return enabled && connHdl != BLE_CONN_HANDLE_INVALID; }
bool bleAdvertising() { return stackUp && Bluefruit.Advertising.isRunning(); }
const char *bleName() { return devName; }

// restartOnDisconnect() does not always re-arm: after one client disconnects the
// board can go quiet with advertising stopped, invisible to every scanner while
// still reporting itself enabled. Re-arm it here instead of trusting the flag.
void blePoll()
{
    if (stackUp && enabled && connHdl == BLE_CONN_HANDLE_INVALID &&
        !Bluefruit.Advertising.isRunning()) {
        Bluefruit.Advertising.start(0);
    }
}

int bleAvailable() { return bleConnected() ? bleuart.available() : 0; }
int bleRead()      { return bleuart.read(); }

size_t bleWrite(const uint8_t *data, size_t len)
{
    if (!bleConnected()) return 0;

    // BLEUart::write returns short when the SoftDevice notification queue is
    // full, which for anything bigger than an ACK means a truncated frame.
    // Feed it one notification at a time and wait for buffers instead.
    BLEConnection *conn = Bluefruit.Connection(connHdl);
    size_t mtu = conn && conn->getMtu() > 3 ? (size_t)conn->getMtu() - 3 : 20;
    size_t sent = 0;
    // Waiting for a full notification queue stalls the whole loop: the serial
    // client stops getting answers and the GPS UART overruns, which shows up as
    // spliced NMEA sentences rather than as a BLE fault. Wait only a fraction of
    // a connection interval, then drop the rest of the frame; the host's CRC
    // discards it and asks again.
    uint32_t deadline = millis() + 25;
    while (sent < len && bleConnected()) {
        size_t n = len - sent < mtu ? len - sent : mtu;
        size_t w = bleuart.write(connHdl, data + sent, n);
        if (w == 0) {
            if ((int32_t)(millis() - deadline) >= 0) break;
            delay(1);
            continue;
        }
        sent += w;
    }
    return sent;
}

#endif
