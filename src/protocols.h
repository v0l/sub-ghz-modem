#pragma once
#include <Arduino.h>

// Higher-level on-air formats built on RadioLib's protocol clients. These all
// take over the radio in direct mode, so the caller re-runs radioInit()
// afterwards to get back to packet operation.

enum ProtoKind : uint8_t {
    PROTO_APRS   = 1,   // AX.25 UI frame with an APRS position or status
    PROTO_AX25   = 2,   // raw AX.25 UI frame
    PROTO_POCSAG = 3,   // pager message, transmit only
    PROTO_RTTY   = 4,
    PROTO_MORSE  = 5,
    PROTO_HELL   = 6,   // Hellschreiber
    PROTO_FSK4   = 7,   // 4-FSK, as used by Horus telemetry
};

struct ProtoRequest {
    uint8_t  kind;
    float    baseMhz;
    uint8_t  data[192];  // payload as bytes; text modes get it NUL terminated
    uint16_t dataLen;
    char     src[8];     // source callsign, or Morse ident
    char     dst[8];     // destination callsign
    char     lat[16];    // APRS format, e.g. 4911.67N
    char     lon[16];    // e.g. 00610.90E
    uint32_t addr;       // POCSAG capcode
    uint32_t shiftHz;
    uint16_t rate;       // baud, or words per minute for Morse
    uint8_t  srcSsid;
    uint8_t  dstSsid;
    uint8_t  symbol;     // APRS symbol character
    uint8_t  encoding;
};

void protoDefaults(ProtoRequest *r, float baseMhz);

// Worst-case framed size for the packet-based kinds, so the caller can refuse a
// payload the radio cannot carry. Zero for the direct-mode kinds, which stream
// and have no packet limit.
size_t protoFramedLen(const ProtoRequest *r);

// Returns a RadioLib status code.
int16_t protoSend(const ProtoRequest *r);
