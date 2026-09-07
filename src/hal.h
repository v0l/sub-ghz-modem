#pragma once
#include <Arduino.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// The physical serial port. Serial on both boards, or UART2 on the T-Beam when
// built with -DMODEM_USE_UART2.
Stream &halPort();

// Every transport the protocol runs over: the serial port, plus BLE when a
// client is connected. See src/link.h.
extern Stream &io;

void halSerialBegin(unsigned long baud);
void halReboot();

// Reboots into the board's firmware update mode, where one exists. Returns
// false when the board has none, leaving the modem running.
bool halBootloader();

// Whole-struct persistence. ESP32 uses an NVS blob, nRF52 a LittleFS file.
bool halSettingsLoad(void *blob, size_t len);
void halSettingsSave(const void *blob, size_t len);

// printf for the modem port. Arduino's Print has no printf outside the ESP32
// core, so route everything through one implementation.
void outf(const char *fmt, ...);
