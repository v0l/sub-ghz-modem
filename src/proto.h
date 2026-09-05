// Wire protocol: a framed, CRC-checked TLV.
//
//   A5 5A  TYPE(u8)  LEN(u16 LE)  VALUE[LEN]  CRC16(u16 LE)
//
// CRC16-CCITT (poly 0x1021, init 0xFFFF) covers TYPE, LEN and VALUE. The magic
// lets a receiver resynchronise after a truncated write, and the length plus
// CRC means a corrupted frame is dropped rather than half-executed.
//
// Config values are themselves TLVs nested in the VALUE field:
//
//   ID(u8)  LEN(u8)  VALUE[LEN]
//
// Multi-byte integers are little endian throughout. Frequencies are in Hz and
// rates in Hz or bit/s, so there are no floats on the wire.

#pragma once
#include <Arduino.h>

#define PROTO_SOF0 0xA5
#define PROTO_SOF1 0x5A
#define PROTO_MAX_VALUE 512

// Host to device
#define MSG_PING        0x01
#define MSG_GET_INFO    0x02
#define MSG_GET_CONFIG  0x03
#define MSG_SET_CONFIG  0x04
#define MSG_TX          0x05
#define MSG_CW          0x06
#define MSG_RX_ENABLE   0x07
#define MSG_SAVE        0x08
#define MSG_LOAD        0x09
#define MSG_RESET       0x0A
#define MSG_GET_STATS   0x0B
#define MSG_DIAG        0x0C
#define MSG_SCAN        0x0D
#define MSG_LED         0x0E  // u8 mask: bit0 red, bit1 green, bit2 blue
#define MSG_PIN         0x0F  // u8 arduino pin, u8 level: drive any pin, for finding LEDs

// Device to host
#define MSG_ACK         0x81
#define MSG_ERR         0x82
#define MSG_INFO        0x83
#define MSG_CONFIG      0x84
#define MSG_TX_DONE     0x85
#define MSG_RX          0x86
#define MSG_STATS       0x87
#define MSG_DIAG_RESULT 0x88
#define MSG_READY       0x89
#define MSG_SCAN_RESULT 0x8A

// Config parameter ids
#define P_MODEM         0x01  // u8
#define P_FREQ          0x02  // u32 Hz
#define P_POWER         0x03  // i8 dBm
#define P_BW            0x04  // u32 Hz
#define P_SF            0x05  // u8
#define P_CR            0x06  // u8
#define P_SYNCWORD      0x07  // u8
#define P_PREAMBLE      0x08  // u16 symbols
#define P_CRC           0x09  // u8
#define P_BITRATE       0x0A  // u32 bit/s
#define P_FDEV          0x0B  // u32 Hz
#define P_RXBW          0x0C  // u32 Hz
#define P_SHAPING       0x0D  // u8
#define P_SYNCBYTES     0x0E  // 0..8 bytes
#define P_FSK_PREAMBLE  0x0F  // u16 bits or bytes, chip dependent
#define P_FSK_CRC       0x10  // u8
#define P_REG_LDO       0x11  // u8
#define P_LR_BW         0x12  // u8
#define P_LR_CR         0x13  // u8
#define P_LR_GRID       0x14  // u8
#define P_FIXED_LEN     0x15  // u8, 0 = variable length with a length byte

// Info field ids, reused inside MSG_INFO
#define I_FW            0x01  // string
#define I_BOARD         0x02  // string
#define I_RADIO         0x03  // string
#define I_MAX_PAYLOAD   0x04  // u16
#define I_BATT_MV       0x05  // u16
#define I_UPTIME_S      0x06  // u32
#define I_POWER_PATH    0x07  // string

// Error codes. Positive values are ours, negative ones are RadioLib's.
#define E_BAD_CRC       1
#define E_BAD_LENGTH    2
#define E_UNKNOWN_MSG   3
#define E_BAD_PARAM     4
#define E_RANGE         5
#define E_UNSUPPORTED   6
#define E_RADIO         7
#define E_TX_ONLY       8
#define E_BUSY          9

// RX flags
#define RXF_CRC_ERROR   0x01

uint16_t protoCrc16(const uint8_t *data, size_t len);

class FrameWriter {
public:
    FrameWriter(uint8_t type) : type_(type), len_(0) {}
    void u8(uint8_t v)  { if (len_ < sizeof(buf_)) buf_[len_++] = v; }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void bytes(const uint8_t *d, size_t n) { for (size_t i = 0; i < n; i++) u8(d[i]); }
    void tlv(uint8_t id, const uint8_t *d, uint8_t n) { u8(id); u8(n); bytes(d, n); }
    void tlvU8(uint8_t id, uint8_t v)   { u8(id); u8(1); u8(v); }
    void tlvU16(uint8_t id, uint16_t v) { u8(id); u8(2); u16(v); }
    void tlvU32(uint8_t id, uint32_t v) { u8(id); u8(4); u32(v); }
    void tlvStr(uint8_t id, const char *s) { tlv(id, (const uint8_t *)s, (uint8_t)strlen(s)); }
    void send(Stream &io) const;

private:
    uint8_t type_;
    uint16_t len_;
    uint8_t buf_[PROTO_MAX_VALUE];
};

// Walks the nested TLVs of a received VALUE field.
class TlvReader {
public:
    TlvReader(const uint8_t *d, size_t n) : d_(d), n_(n), pos_(0) {}
    bool next(uint8_t *id, const uint8_t **val, uint8_t *len);
    static uint32_t toU32(const uint8_t *v, uint8_t len);

private:
    const uint8_t *d_;
    size_t n_, pos_;
};
