#pragma once
#include <Arduino.h>

// The protocol runs over the serial port and, on boards built with BLE, over a
// Nordic UART service at the same time. Each transport gets its own frame
// parser, because one shared state machine would interleave two clients' bytes
// into garbage.

typedef void (*LinkHandler)(uint8_t type, const uint8_t *val, uint16_t len);

void linkInit();

// Reads both transports and dispatches every complete frame. A reply written
// while the handler runs goes back to the transport the frame came from;
// anything written outside it, such as a received packet or an NMEA sentence,
// goes to every connected transport.
void linkPoll(LinkHandler handler);

// Implemented by main.cpp: emits MSG_ERR and counts the failure.
void linkError(uint8_t reason, int16_t code);
