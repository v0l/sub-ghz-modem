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

// Whether a host is on the serial port, which a USB bridge cannot answer: DTR
// is not wired to the MCU on any of these boards, so the only evidence of a
// host is a frame it sent. True for LINK_SEEN_MS after the last valid one.
#define LINK_SEEN_MS 15000UL
bool linkSerialSeen();

// Implemented by main.cpp: emits MSG_ERR and counts the failure.
void linkError(uint8_t reason, int16_t code);
