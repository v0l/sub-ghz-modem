#include "link.h"
#include "hal.h"
#include "ble_uart.h"
#include "proto.h"

enum Target : uint8_t { T_ALL, T_SERIAL, T_BLE };
static Target target = T_ALL;

// One instance per transport. The reply target follows whichever parser is
// dispatching, so two clients never see each other's answers.
class Parser {
public:
    explicit Parser(Target src) : src_(src) {}
    void feed(uint8_t c, LinkHandler handler);

private:
    Target src_;
    uint8_t state_ = 0, type_ = 0;
    uint16_t want_ = 0, got_ = 0, crcGot_ = 0;
    uint8_t val_[PROTO_MAX_VALUE];
};

void Parser::feed(uint8_t c, LinkHandler handler)
{
    switch (state_) {
        case 0: if (c == PROTO_SOF0) state_ = 1; break;
        // A second SOF0 keeps us waiting rather than dropping a real frame that
        // follows a stray byte.
        case 1: state_ = (c == PROTO_SOF1) ? 2 : (c == PROTO_SOF0 ? 1 : 0); break;
        case 2: type_ = c; state_ = 3; break;
        case 3: want_ = c; state_ = 4; break;
        case 4:
            want_ |= (uint16_t)c << 8;
            if (want_ > PROTO_MAX_VALUE) {
                target = src_;
                linkError(E_BAD_LENGTH, (int16_t)want_);
                target = T_ALL;
                state_ = 0;
                break;
            }
            got_ = 0;
            state_ = want_ ? 5 : 6;
            break;
        case 5:
            val_[got_++] = c;
            if (got_ >= want_) state_ = 6;
            break;
        case 6: crcGot_ = c; state_ = 7; break;
        case 7: {
            crcGot_ |= (uint16_t)c << 8;
            uint8_t head[3] = { type_, (uint8_t)(want_ & 0xFF), (uint8_t)(want_ >> 8) };
            uint16_t crc = protoCrc16(head, 3);
            // Continue the CRC across the value with the same seed.
            for (uint16_t i = 0; i < want_; i++) {
                crc ^= (uint16_t)val_[i] << 8;
                for (int b = 0; b < 8; b++)
                    crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
            }
            target = src_;
            if (crc == crcGot_) handler(type_, val_, want_);
            else linkError(E_BAD_CRC, (int16_t)crcGot_);
            target = T_ALL;
            state_ = 0;
            break;
        }
    }
}

static Parser serialParser(T_SERIAL);
static Parser bleParser(T_BLE);

// Fans a frame out to the serial port and to BLE, or back to one of them while
// a handler is replying.
class LinkStream : public Stream {
public:
    int available() override { return halPort().available() + bleAvailable(); }
    int read() override {
        int c = halPort().read();
        return c >= 0 ? c : bleRead();
    }
    int peek() override { return halPort().peek(); }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *b, size_t n) override {
        if (target != T_BLE) halPort().write(b, n);
        if (target != T_SERIAL) bleWrite(b, n);
        return n;
    }
    void flush() override { halPort().flush(); }
};

static LinkStream links;   // not "link": ESP32 pulls in POSIX link() from unistd.h
Stream &io = links;

void linkInit()
{
    bleInit();
}

void linkPoll(LinkHandler handler)
{
    blePoll();
    while (halPort().available()) serialParser.feed((uint8_t)halPort().read(), handler);
    while (bleAvailable() > 0) {
        int c = bleRead();
        if (c < 0) break;
        bleParser.feed((uint8_t)c, handler);
    }
}
