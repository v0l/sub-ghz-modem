#include "gps.h"

#if HAS_GPS

#ifndef GPS_BAUD
#define GPS_BAUD 9600
#endif

// NMEA 0183 caps a sentence at 82 characters including $ and CRLF. Anything
// longer is a receiver talking a proprietary dialect, and is dropped.
#define NMEA_MAX 82

static HardwareSerial &gpsPort = Serial1;

// A receiver that loses the sky keeps the last RMC values but flags them
// invalid; one that is unplugged says nothing at all. Both must stop looking
// like a position, so a fix expires on its own.
#define FIX_MAX_AGE_MS 30000

static bool feedOn = false;
static bool fix = false;
static uint32_t fixMs = 0;
static char line[NMEA_MAX + 1];
static size_t lineLen = 0;
static bool overrun = false;
static char fixLat[12];
static char fixLon[13];

// Copies field n (comma separated, field 0 is the sentence id) into dst.
static bool nmeaField(const char *s, int n, char *dst, size_t cap)
{
    int field = 0;
    for (const char *p = s; ; p++) {
        if (field == n && (*p == ',' || *p == '*' || *p == '\0')) break;
        if (*p == '\0' || *p == '*') return false;
        if (*p == ',') { field++; continue; }
        if (field != n) continue;
        if (cap <= 1) return false;
        *dst++ = *p;
        cap--;
    }
    *dst = '\0';
    return true;
}

static bool nmeaChecksumOk(const char *s, size_t len)
{
    if (len < 4 || s[0] != '$') return false;
    const char *star = nullptr;
    for (size_t i = len; i-- > 1; ) {
        if (s[i] == '*') { star = s + i; break; }
    }
    if (!star || (size_t)(star - s) + 3 > len) return false;

    uint8_t sum = 0;
    for (const char *p = s + 1; p < star; p++) sum ^= (uint8_t)*p;

    char want[3] = { star[1], star[2], '\0' };
    return (uint8_t)strtol(want, nullptr, 16) == sum;
}

// NMEA gives ddmm.mmmm; APRS wants ddmm.hh plus the hemisphere. Same layout,
// two decimals, so this is a truncation rather than a conversion.
static bool toAprs(const char *raw, const char *hemi, int degDigits,
                   char *dst, size_t cap)
{
    size_t want = (size_t)degDigits + 5;   // dd + mm.hh
    if (strlen(raw) < want || strlen(hemi) != 1 || cap < want + 2) return false;
    if (raw[degDigits + 2] != '.') return false;
    memcpy(dst, raw, want);
    dst[want] = hemi[0];
    dst[want + 1] = '\0';
    return true;
}

// Only RMC is parsed. Every receiver emits it, it carries the validity flag,
// and GGA would add a second code path for the same two numbers.
static void parseFix(const char *s, size_t len)
{
    if (len < 7 || s[0] != '$') return;
    if (strncmp(s + 3, "RMC", 3) != 0) return;
    if (!nmeaChecksumOk(s, len)) return;

    char status[4], lat[16], ns[4], lon[16], ew[4];
    if (!nmeaField(s, 2, status, sizeof(status)) || status[0] != 'A') {
        fix = false;
        return;
    }
    if (!nmeaField(s, 3, lat, sizeof(lat)) || !nmeaField(s, 4, ns, sizeof(ns)) ||
        !nmeaField(s, 5, lon, sizeof(lon)) || !nmeaField(s, 6, ew, sizeof(ew))) {
        return;
    }
    char a[12], o[13];
    if (!toAprs(lat, ns, 2, a, sizeof(a))) return;
    if (!toAprs(lon, ew, 3, o, sizeof(o))) return;

    strcpy(fixLat, a);
    strcpy(fixLon, o);
    fix = true;
    fixMs = millis();
}

void gpsInit()
{
#if defined(BOARD_TBEAM)
    gpsPort.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
#else
    gpsPort.begin(GPS_BAUD);   // the variant already pins Serial1 at the receiver
#endif
}

void gpsFeed(bool on)
{
    feedOn = on;
}

void gpsPoll(GpsSink sink)
{
    while (gpsPort.available()) {
        char c = (char)gpsPort.read();
        if (c == '\r') continue;
        if (c != '\n') {
            if (lineLen < NMEA_MAX) line[lineLen++] = c;
            else overrun = true;
            continue;
        }
        line[lineLen] = '\0';
        if (!overrun && lineLen >= 6 && (line[0] == '$' || line[0] == '!')) {
            parseFix(line, lineLen);
            if (feedOn && sink) sink(line, lineLen);
        }
        lineLen = 0;
        overrun = false;
    }
}

bool gpsHasFix()
{
    return fix && (millis() - fixMs) < FIX_MAX_AGE_MS;
}

bool gpsPosition(char *lat, size_t latCap, char *lon, size_t lonCap)
{
    if (!gpsHasFix()) return false;
    if (latCap <= strlen(fixLat) || lonCap <= strlen(fixLon)) return false;
    strcpy(lat, fixLat);
    strcpy(lon, fixLon);
    return true;
}

const char *gpsStatus()
{
    if (!feedOn && !gpsHasFix()) return "off";
    return gpsHasFix() ? "fix" : "no fix";
}

#else  // no receiver on this board

void gpsInit() {}
void gpsFeed(bool) {}
void gpsPoll(GpsSink) {}
bool gpsHasFix() { return false; }
bool gpsPosition(char *, size_t, char *, size_t) { return false; }
const char *gpsStatus() { return "none"; }

#endif
