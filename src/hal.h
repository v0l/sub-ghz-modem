#pragma once
#include <Arduino.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// The port the modem protocol talks on. Serial on both boards, or UART2 on the
// T-Beam when built with -DMODEM_USE_UART2.
extern Stream &io;

void halSerialBegin(unsigned long baud);
void halReboot();

// Whole-struct persistence. ESP32 uses an NVS blob, nRF52 a LittleFS file.
bool halSettingsLoad(void *blob, size_t len);
void halSettingsSave(const void *blob, size_t len);

// printf for the modem port. Arduino's Print has no printf outside the ESP32
// core, so route everything through one implementation.
void outf(const char *fmt, ...);
