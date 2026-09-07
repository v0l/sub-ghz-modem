#pragma once
#include <Arduino.h>
#include "board.h"

// Named ble_uart.h, not ble.h: the SoftDevice ships its own ble.h and -Isrc
// comes first, so ours would shadow it inside the Bluefruit stack.
// The same framed TLV protocol, carried over a Nordic UART service so a phone
// or a host with a Bluetooth adapter can drive the modem without a cable.
// Opt in with -DENABLE_BLE; the Nucleo has no radio for it.
#if defined(ENABLE_BLE) && defined(BOARD_TECHO)
#define HAS_BLE 1
#elif defined(ENABLE_BLE) && defined(BOARD_TBEAM)
#define HAS_BLE 1
#else
#define HAS_BLE 0
#endif

// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E and friends, Nordic's UART service.
// Every generic BLE terminal already knows them.
#define BLE_UART_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UART_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // host writes
#define BLE_UART_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // device notifies

// Our own marker, advertised alongside the UART service so a host can tell a
// sub-ghz-modem from every other NUS device without connecting to it. The
// leading A55A is the frame magic from src/proto.h.
#define BLE_MODEM_SERVICE "A55A0001-5A5A-4D4D-8D45-4D0000A55A5A"

void bleInit();
void bleEnable(bool on);
bool bleEnabled();
bool bleConnected();
bool bleAdvertising();
const char *bleName();

// Advertising restarts and other stack housekeeping. Called every loop.
void blePoll();

int bleAvailable();
int bleRead();
size_t bleWrite(const uint8_t *data, size_t len);
