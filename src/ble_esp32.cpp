#include "ble_uart.h"

#if HAS_BLE && defined(BOARD_TBEAM)

#include <NimBLEDevice.h>

#define BLE_RX_BUF 1024

static NimBLEServer *server = nullptr;
static NimBLECharacteristic *txChar = nullptr;
static bool enabled = false;
static volatile bool connected = false;
static char devName[20];

// Written from the NimBLE host task, drained from loop(), so the indices are
// single-producer single-consumer and need no lock.
static uint8_t rxBuf[BLE_RX_BUF];
static volatile uint16_t rxHead = 0, rxTail = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s) override {
        connected = true;
        s->updateConnParams(s->getPeerIDInfo(0).getConnHandle(), 12, 24, 0, 200);
    }
    void onDisconnect(NimBLEServer *) override {
        connected = false;
        if (enabled) NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        const std::string &v = c->getValue();
        for (size_t i = 0; i < v.size(); i++) {
            uint16_t next = (uint16_t)((rxHead + 1) % BLE_RX_BUF);
            if (next == rxTail) return;   // full: drop, the CRC will catch it
            rxBuf[rxHead] = (uint8_t)v[i];
            rxHead = next;
        }
    }
};

void bleInit()
{
    uint64_t mac = ESP.getEfuseMac();
    snprintf(devName, sizeof(devName), "modem-%04X", (unsigned)(mac & 0xFFFF));

    NimBLEDevice::init(devName);
    NimBLEDevice::setMTU(247);
    server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService *svc = server->createService(BLE_UART_SERVICE);
    txChar = svc->createCharacteristic(BLE_UART_TX, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *rxChar = svc->createCharacteristic(
        BLE_UART_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new RxCallbacks());
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_UART_SERVICE);
    // Two 128-bit UUIDs do not fit in one 31 byte advert, so the marker that
    // says "this is a sub-ghz-modem" rides in the scan response.
    NimBLEAdvertisementData scan;
    scan.setName(devName);
    scan.setCompleteServices(NimBLEUUID(BLE_MODEM_SERVICE));
    adv->setScanResponseData(scan);
    adv->setScanResponse(true);
}

void bleEnable(bool on)
{
    enabled = on;
    if (on) {
        NimBLEDevice::startAdvertising();
    } else {
        NimBLEDevice::stopAdvertising();
        if (connected && server->getConnectedCount()) {
            server->disconnect(server->getPeerIDInfo(0).getConnHandle());
        }
    }
}

bool bleEnabled()   { return enabled; }
bool bleConnected() { return enabled && connected; }
bool bleAdvertising() { return NimBLEDevice::getAdvertising()->isAdvertising(); }
const char *bleName() { return devName; }

void blePoll() {}   // the NimBLE host task does its own work

int bleAvailable()
{
    return (int)((rxHead + BLE_RX_BUF - rxTail) % BLE_RX_BUF);
}

int bleRead()
{
    if (rxHead == rxTail) return -1;
    uint8_t c = rxBuf[rxTail];
    rxTail = (uint16_t)((rxTail + 1) % BLE_RX_BUF);
    return c;
}

size_t bleWrite(const uint8_t *data, size_t len)
{
    if (!bleConnected() || !txChar) return 0;
    // A notification cannot exceed the negotiated MTU less the ATT header, and
    // an oversized one is dropped silently rather than split for you.
    size_t chunk = NimBLEDevice::getMTU() > 3 ? NimBLEDevice::getMTU() - 3 : 20;
    for (size_t off = 0; off < len; off += chunk) {
        size_t n = len - off < chunk ? len - off : chunk;
        txChar->setValue(data + off, n);
        txChar->notify();
        delay(0);   // let the host task drain its buffers between packets
    }
    return len;
}

#endif
