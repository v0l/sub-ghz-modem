#pragma once
#include <Arduino.h>
#include "board.h"

#if defined(GPS_RX) && defined(GPS_TX)
#define HAS_GPS 1
#else
#define HAS_GPS 0
#endif

typedef void (*GpsSink)(const char *line, size_t len);

void gpsInit();

// Turns the host-facing NMEA feed on or off. Position tracking runs either way,
// so a fix is available for APRS without flooding the link with sentences.
void gpsFeed(bool on);

// Reads whatever the receiver has sent, hands complete sentences to sink when
// the feed is on, and updates the last known fix. Cheap to call every loop.
void gpsPoll(GpsSink sink);

bool gpsHasFix();

// Last fix in APRS format, ddmm.hhN and dddmm.hhE. False when there is no fix.
bool gpsPosition(char *lat, size_t latCap, char *lon, size_t lonCap);

// "none", "off", "no fix" or "fix", for the info banner.
const char *gpsStatus();
