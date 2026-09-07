#include "ble_uart.h"

#if !HAS_BLE

void bleInit() {}
void bleEnable(bool) {}
bool bleEnabled() { return false; }
bool bleConnected() { return false; }
bool bleAdvertising() { return false; }
const char *bleName() { return "none"; }
void blePoll() {}
int bleAvailable() { return 0; }
int bleRead() { return -1; }
size_t bleWrite(const uint8_t *, size_t) { return 0; }

#endif
