// Generic sub-GHz radio modem driven entirely over a serial link.
// Boards: LilyGO T-Beam (ESP32, SX1276/SX1262) and T-Echo (nRF52840, SX1262).
// Modems: LoRa, (G)FSK, OOK (SX1276 only), LR-FHSS (SX1262 only, opt-in).
// Framed binary TLV protocol on the serial link, see src/proto.h and README.md.

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "board.h"
#include "hal.h"
#include "proto.h"
#include "display.h"
#include "protocols.h"
#include "radio.h"
#include "gps.h"
#include "link.h"
#include "ble_uart.h"

#if defined(BOARD_TECHO) && !defined(RADIO_SX1262)
#define RADIO_SX1262        // the T-Echo has no other option
#endif
#if defined(BOARD_NUCLEO_WL55) && !defined(RADIO_STM32WLX)
#define RADIO_STM32WLX      // the radio is on the MCU die
#endif

#if defined(RADIO_STM32WLX)
// STM32WLx derives from SX1262, so every begin/setter below is the same call.
typedef STM32WLx RadioBase;
#define RADIO_NAME "STM32WLx"
#define PWR_MIN (-17)       // low-power PA floor; RadioLib picks LP or HP for you
#define PWR_MAX 22
#define HAS_OOK 0
#define RADIO_IS_SX126X 1
#elif defined(RADIO_SX1262)
typedef SX1262 RadioBase;
#define RADIO_NAME "SX1262"
#define PWR_MIN (-9)
#define PWR_MAX 22
#define HAS_OOK 0
#define RADIO_IS_SX126X 1
#elif defined(RADIO_SX1276)
typedef SX1276 RadioBase;
#define RADIO_NAME "SX1276"
#define PWR_MIN (-3)
#define PWR_MAX 20
#define HAS_OOK 1
#define RADIO_IS_SX126X 0
#elif defined(RADIO_SX1280) || defined(RADIO_SX1281)
// RadioLib picks the driver by the chip's version string register, and the
// SX1281 on a RadioMaster RP2 reports "SX1280" there despite the marking on the
// package. The SX1281 class rejects it outright, so build these boards as an
// SX1280: the classes differ only in the ranging engine an SX1281 lacks and
// this modem never uses.
#ifdef RADIO_SX1281
typedef SX1281 RadioBase;
#define RADIO_NAME "SX1281"
#else
typedef SX1280 RadioBase;
#define RADIO_NAME "SX1280"
#endif
#define PWR_MIN (-18)
#define PWR_MAX 13          // +12.5 dBm, and RadioLib takes whole dBm
#define HAS_OOK 0
#define RADIO_IS_SX126X 0
#else
#error "Build with -DRADIO_SX1276, -DRADIO_SX1262, -DRADIO_STM32WLX or -DRADIO_SX1280"
#endif

// The SX128x is the only family with the long interleaved coding rates and the
// only one whose FSK mode is reached through beginGFSK().
#if defined(RADIO_SX1280) || defined(RADIO_SX1281)
#define RADIO_IS_SX128X 1
#else
#define RADIO_IS_SX128X 0
#endif

// Boards that inherit the SX127x-shaped defaults say nothing; the RP2 cannot,
// because 125 kHz is not a bandwidth an SX128x has.
#ifndef DEFAULT_BW
#define DEFAULT_BW 125.0f
#endif
#ifndef DEFAULT_SF
#define DEFAULT_SF 9
#endif
#ifndef DEFAULT_POWER
#define DEFAULT_POWER 17
#endif

#if defined(ENABLE_LRFHSS) && !RADIO_IS_SX126X
#error "LR-FHSS needs an SX126x-class radio"
#endif

// getDeviceErrors() and readRegister() are protected in RadioLib, and DIAG needs
// both to tell a configuration problem from a chip-level failure.
class ModemRadio : public RadioBase {
public:
    explicit ModemRadio(Module *m) : RadioBase(m) {}
#if RADIO_IS_SX126X
    uint16_t diagErrors() { return this->getDeviceErrors(); }
    uint8_t diagOcp() {
        uint8_t v = 0;
        this->readRegister(RADIOLIB_SX126X_REG_OCP_CONFIGURATION, &v, 1);
        return v;
    }
#endif
#if RADIO_IS_SX128X
    // begin() reports one error for a dead bus and for a chip whose identity
    // string is not the one RadioLib expects. Sixteen bytes of 0x00 or 0xFF is
    // the first, anything legible is the second.
    void diagVersion(uint8_t *out) {
        Module *m = this->getMod();
        m->init();
        m->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
        m->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
        m->spiConfig.statusPos = 1;
        m->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = RADIOLIB_SX128X_CMD_READ_REGISTER;
        m->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = RADIOLIB_SX128X_CMD_WRITE_REGISTER;
        m->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP] = RADIOLIB_SX128X_CMD_NOP;
        m->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = RADIOLIB_SX128X_CMD_GET_STATUS;
        m->spiConfig.stream = true;
        // No status parser: a raw read is what we want, errors and all.
        this->reset(true);
        m->SPIreadRegisterBurst(RADIOLIB_SX128X_REG_VERSION_STRING, 16, out);
    }
#endif
};

#if defined(RADIO_STM32WLX)
static ModemRadio radio(new STM32WLx_Module());
#elif defined(RADIO_SX1262) || defined(RADIO_SX1280) || defined(RADIO_SX1281)
static ModemRadio radio(new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY));
#else
static ModemRadio radio(new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1));
#endif

PhysicalLayer *radioPhy() { return &radio; }
static bool radioInit();
bool radioPacketMode() { return radioInit(); }

#ifdef BOARD_NUCLEO_WL55
// Front-end switch wiring for the Nucleo-WL55JC. Other WL boards differ, and a
// wrong table shows up as a working radio that neither transmits nor hears.
static const uint32_t rfswitchPins[] = { WL_RFSW_1, WL_RFSW_2, WL_RFSW_3,
                                         RADIOLIB_NC, RADIOLIB_NC };
static const Module::RfSwitchMode_t rfswitchTable[] = {
    { STM32WLx::MODE_IDLE,  { LOW,  LOW,  LOW  } },
    { STM32WLx::MODE_RX,    { HIGH, HIGH, LOW  } },
    { STM32WLx::MODE_TX_LP, { HIGH, HIGH, HIGH } },
    { STM32WLx::MODE_TX_HP, { HIGH, LOW,  HIGH } },
    END_OF_MODE_TABLE,
};
#endif

#define FW_VERSION   "1.3.0"
#define CFG_MAGIC    0x4D475A53UL   // "SZGM"
#define CFG_VERSION  10
#define MAX_PAYLOAD  255
#if RADIO_IS_SX126X
#define MAX_FSK_PAYLOAD 255
#else
// SX127x FSK has a 64 byte FIFO, so asking for more fails with PACKET_TOO_LONG
// and takes the whole radio init down with it.
#define MAX_FSK_PAYLOAD 63
#endif
#define MAX_SYNC     8
#define CMD_BUF_LEN  (MAX_PAYLOAD * 2 + 32)

enum ModemMode : uint8_t { MODE_LORA = 0, MODE_FSK = 1, MODE_OOK = 2, MODE_LRFHSS = 3 };

struct Config {
    uint32_t magic;
    uint16_t version;

    uint8_t  modem;
    float    freq;      // MHz, shared by every mode
    int8_t   power;     // dBm, shared

    // LoRa
    float    bw;        // kHz
    uint8_t  sf;        // 6..12
    uint8_t  cr;        // 4/x denominator, 5..8
    uint8_t  syncWord;
    uint16_t preamble;  // symbols
    bool     crc;
    // Implicit header mode: no length, rate or CRC flag on the air, so both ends
    // have to be told the payload size. 0 keeps the explicit header. ExpressLRS
    // and every other fixed-frame LoRa link runs implicit.
    uint8_t  loraImplicit;
    // The SX128x long interleaved coding rates, which Semtech names and does not
    // describe. Not a rate of its own: it re-arranges the same 4/5 .. 4/8.
    bool     crLongInterleave;

    // FSK / OOK
    float    br;        // kbps
    float    fdev;      // kHz, ignored for OOK
    float    rxbw;      // kHz
    uint8_t  shaping;   // RADIOLIB_SHAPING_*
    uint8_t  syncLen;   // 0..8, 0 disables sync word detection
    uint8_t  syncBytes[MAX_SYNC];
    uint16_t fskPreamble;
    bool     fskCrc;
    // Raw over-the-air formats such as Fine Offset carry no length byte, so the
    // receiver has to be told how many bytes a frame is.
    uint8_t  fixedLen;

    // LR-FHSS
    uint8_t  lrBw;
    uint8_t  lrCr;
    bool     lrNarrowGrid;

    // PA supply. The STM32WL SMPS is shared with the MCU and the Arduino core
    // never enables it, so asking the radio to run its PA from DC-DC starves
    // the amplifier while leaving receive perfectly healthy.
    bool     regLdo;

    bool     gpsFeed;   // stream NMEA sentences to the host
    bool     bleOn;     // advertise the BLE UART
};

static const Config defaults = {
    CFG_MAGIC, CFG_VERSION,
    MODE_LORA, DEFAULT_FREQ, DEFAULT_POWER,
    DEFAULT_BW, DEFAULT_SF, 7, 0x12, 8, true, 0, false,
    4.8f, 25.0f, 58.6f, RADIOLIB_SHAPING_NONE, 2, {0x2D, 0xD4, 0, 0, 0, 0, 0, 0}, 16, true, 0,
    0, 0, false,
#ifdef DEFAULT_REG_LDO
    DEFAULT_REG_LDO,
#else
    false,
#endif
    false,
    true,
};

static Config cfg;

static volatile bool rxFlag = false;
static bool busy = false;      // transmitting; ignore radio IRQs
static bool rxEnabled = true;
static bool rxCapable = true;  // false in LR-FHSS, which RadioLib supports TX-only

static uint32_t txCount = 0, rxCount = 0, errCount = 0;
static const char *powerPath = "?";

// Red while the receiver is armed, blue while transmitting. Boards with one LED
// map both to it, so TX simply wins for the duration of the packet.
static void ledWrite(uint8_t pin, bool on)
{
    digitalWrite(pin, LED_ACTIVE_LOW ? !on : on);
}

static void ledInit()
{
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    ledWrite(LED_RED, false);
    ledWrite(LED_GREEN, false);
    ledWrite(LED_BLUE, false);
}

// ---------------------------------------------------------------- helpers

static const char *modeName(uint8_t m)
{
    switch (m) {
        case MODE_LORA:   return "LORA";
        case MODE_FSK:    return "FSK";
        case MODE_OOK:    return "OOK";
        case MODE_LRFHSS: return "LRFHSS";
        default:          return "?";
    }
}

static void IRAM_ATTR onRadioIrq()
{
    rxFlag = true;
}

static void fail(uint8_t reason, int16_t code)
{
    errCount++;
    FrameWriter f(MSG_ERR);
    f.u8(reason);
    f.u16((uint16_t)code);
    f.send(io);
}

static const char *shapingName(uint8_t s)
{
    switch (s) {
        case RADIOLIB_SHAPING_0_3: return "0.3";
        case RADIOLIB_SHAPING_0_5: return "0.5";
        case RADIOLIB_SHAPING_1_0: return "1.0";
        default:                   return "NONE";
    }
}

// ---------------------------------------------------------------- radio setup

static void startRx();

// Full re-init on every parameter change. Switching modulation needs it anyway,
// and it keeps one code path instead of a matrix of per-mode setters.
static bool radioInit()
{
    int16_t s;
    rxCapable = true;

    if (cfg.modem == MODE_LORA) {
#if RADIO_IS_SX126X
        s = radio.begin(cfg.freq, cfg.bw, cfg.sf, cfg.cr, cfg.syncWord,
                        cfg.power, cfg.preamble, LORA_TCXO_V, cfg.regLdo);
#ifdef RADIO_SX1262
        if (s == RADIOLIB_ERR_NONE) radio.setDio2AsRfSwitch(true);
#endif
#else
        s = radio.begin(cfg.freq, cfg.bw, cfg.sf, cfg.cr, cfg.syncWord,
                        cfg.power, cfg.preamble);
#endif
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }
#if RADIO_IS_SX128X
        // begin() takes the coding rate but not the interleave flag, so the rate
        // is set a second time to carry it.
        if (cfg.crLongInterleave) {
            if ((s = radio.setCodingRate(cfg.cr, true)) != RADIOLIB_ERR_NONE) {
                fail(E_RADIO, s); return false;
            }
        }
        // On the SX128x the argument is a CRC length in bytes, not a flag.
        s = radio.setCRC(cfg.crc ? 2 : 0);
#else
        s = radio.setCRC(cfg.crc);
#endif
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

        s = cfg.loraImplicit ? radio.implicitHeader(cfg.loraImplicit)
                             : radio.explicitHeader();
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

    } else if (cfg.modem == MODE_FSK || cfg.modem == MODE_OOK) {
#if RADIO_IS_SX128X
        if (cfg.modem == MODE_OOK) { fail(E_UNSUPPORTED, 0); return false; }
        // The SX128x FSK mode is GFSK only, its bit rate is whole kbps, and the
        // receive bandwidth follows the rate rather than being set.
        s = radio.beginGFSK(cfg.freq, (uint16_t)lroundf(cfg.br), cfg.fdev,
                            cfg.power, cfg.fskPreamble);
#elif RADIO_IS_SX126X
        if (cfg.modem == MODE_OOK) { fail(E_UNSUPPORTED, 0); return false; }
        s = radio.beginFSK(cfg.freq, cfg.br, cfg.fdev, cfg.rxbw,
                           cfg.power, cfg.fskPreamble, LORA_TCXO_V, cfg.regLdo);
#ifdef RADIO_SX1262
        if (s == RADIOLIB_ERR_NONE) radio.setDio2AsRfSwitch(true);
#endif
#else
        s = radio.beginFSK(cfg.freq, cfg.br, cfg.fdev, cfg.rxbw,
                           cfg.power, cfg.fskPreamble, cfg.modem == MODE_OOK);
#endif
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

        if (cfg.fixedLen) s = radio.fixedPacketLengthMode(cfg.fixedLen);
        else              s = radio.variablePacketLengthMode(MAX_FSK_PAYLOAD);
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

        if ((s = radio.setDataShaping(cfg.shaping)) != RADIOLIB_ERR_NONE) {
            fail(E_RADIO, s); return false;
        }
        if (cfg.syncLen > 0) {
            if ((s = radio.setSyncWord(cfg.syncBytes, cfg.syncLen)) != RADIOLIB_ERR_NONE) {
                fail(E_RADIO, s); return false;
            }
        }
#if RADIO_IS_SX126X || RADIO_IS_SX128X
        // On SX126x and SX128x the FSK setCRC argument is a length in bytes.
        s = radio.setCRC(cfg.fskCrc ? 2 : 0);
#else
        s = radio.setCRC(cfg.fskCrc);
#endif
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

    } else if (cfg.modem == MODE_LRFHSS) {
#ifdef ENABLE_LRFHSS
        s = radio.beginLRFHSS(cfg.freq, cfg.lrBw, cfg.lrCr, cfg.lrNarrowGrid,
                              cfg.power, LORA_TCXO_V, cfg.regLdo);
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }
#ifdef RADIO_SX1262
        radio.setDio2AsRfSwitch(true);
#endif
        rxCapable = false;   // LR-FHSS is transmit-only in RadioLib
#else
        fail(E_UNSUPPORTED, 0);
        return false;
#endif
    } else {
        fail(E_BAD_PARAM, P_MODEM);
        return false;
    }

#if RADIO_IS_SX126X
    // RadioLib's begin() leaves OCP at 60 mA and setOutputPower() preserves
    // whatever it finds, so the high-power PA gets current-clamped and radiates
    // barely above the noise floor. The SX1262 datasheet wants 140 mA for the
    // HP path, 60 mA for LP.
    if ((s = radio.setCurrentLimit(cfg.power > 14 ? 140.0f : 60.0f)) != RADIOLIB_ERR_NONE) {
        fail(E_RADIO, s); return false;
    }
#endif

    radio.setPacketReceivedAction(onRadioIrq);
    return true;
}

static void startRx()
{
    if (!rxEnabled || !rxCapable) { ledWrite(LED_RX, false); return; }
    int16_t s = radio.startReceive();
    if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); ledWrite(LED_RX, false); return; }
    ledWrite(LED_RX, true);
}

static bool reconfigure()
{
    if (!radioInit()) return false;
    startRx();
    return true;
}

// ---------------------------------------------------------------- persistence

static void loadConfig()
{
    Config tmp;
    if (halSettingsLoad(&tmp, sizeof(tmp)) &&
        tmp.magic == CFG_MAGIC && tmp.version == CFG_VERSION) {
        cfg = tmp;
    } else {
        cfg = defaults;
    }
}

static void saveConfig()
{
    cfg.magic = CFG_MAGIC;
    cfg.version = CFG_VERSION;
    halSettingsSave(&cfg, sizeof(cfg));
}

// ---------------------------------------------------------------- tx / rx

static bool txActive = false;
static uint32_t txStart = 0;
static uint16_t txLen = 0;

static void doTransmit(const uint8_t *data, size_t len)
{
    size_t cap = (cfg.modem == MODE_LORA) ? MAX_PAYLOAD : MAX_FSK_PAYLOAD;
    if (len == 0 || len > cap) { fail(E_BAD_LENGTH, (int16_t)len); return; }
    if (txActive || busy) { fail(E_BUSY, 0); return; }

    ledWrite(LED_RX, false);
    ledWrite(LED_TX, true);
    int16_t s = radio.startTransmit((uint8_t *)data, len);
    if (s != RADIOLIB_ERR_NONE) {
        ledWrite(LED_TX, false);
        fail(E_RADIO, s);
        startRx();
        return;
    }
    // Deliberately non-blocking: a SF12 packet is 2.5 s of airtime, and the old
    // blocking transmit() left the UART unserviced long enough to overrun.
    txActive = true;
    txStart = millis();
    txLen = (uint16_t)len;
    rxFlag = false;
}

static void finishTransmit()
{
    uint32_t dt = millis() - txStart;
    radio.finishTransmit();
    ledWrite(LED_TX, false);
    txActive = false;
    rxFlag = false;

    txCount++;
    FrameWriter f(MSG_TX_DONE);
    f.u16(txLen);
    f.u32(dt);
    f.send(io);

    startRx();
}

static void drainRx()
{
    if (!rxFlag || busy || txActive) return;
    rxFlag = false;

    size_t len = radio.getPacketLength();
    if (len == 0 || len > MAX_PAYLOAD) { startRx(); rxFlag = false; return; }

    uint8_t buf[MAX_PAYLOAD];
    int16_t s = radio.readData(buf, len);

    if (s == RADIOLIB_ERR_NONE || s == RADIOLIB_ERR_CRC_MISMATCH) {
        rxCount++;
        FrameWriter f(MSG_RX);
        f.u16((int16_t)lroundf(radio.getRSSI() * 10.0f));
        f.u16((int16_t)lroundf((cfg.modem == MODE_LORA ? radio.getSNR() : 0.0f) * 10.0f));
        f.u8(s == RADIOLIB_ERR_CRC_MISMATCH ? RXF_CRC_ERROR : 0);
        f.bytes(buf, len);
        f.send(io);
    } else {
        fail(E_RADIO, s);
    }
    startRx();
    rxFlag = false;   // drop any interrupt raised by our own re-arm
}

static void sendNmea(const char *line, size_t len)
{
    FrameWriter f(MSG_NMEA);
    f.bytes((const uint8_t *)line, len);
    f.send(io);
}

// ---------------------------------------------------------------- replies

static void sendAck()
{
    FrameWriter f(MSG_ACK);
    f.send(io);
}

static void sendConfig()
{
    FrameWriter f(MSG_CONFIG);
    f.tlvU8(P_MODEM, cfg.modem);
    // 2.4 GHz in Hz overflows a 32 bit signed long, which is what lroundf
    // returns, and the readback saturates at 2147.483647 MHz.
    f.tlvU32(P_FREQ, (uint32_t)llroundf(cfg.freq * 1000000.0f));
    f.tlvU8(P_POWER, (uint8_t)cfg.power);
    f.tlvU8(P_REG_LDO, cfg.regLdo ? 1 : 0);

    if (cfg.modem == MODE_LORA) {
        f.tlvU32(P_BW, (uint32_t)lroundf(cfg.bw * 1000.0f));
        f.tlvU8(P_SF, cfg.sf);
        f.tlvU8(P_CR, cfg.cr);
        f.tlvU8(P_SYNCWORD, cfg.syncWord);
        f.tlvU16(P_PREAMBLE, cfg.preamble);
        f.tlvU8(P_CRC, cfg.crc ? 1 : 0);
        f.tlvU8(P_IMPLICIT, cfg.loraImplicit);
        f.tlvU8(P_CR_LI, cfg.crLongInterleave ? 1 : 0);
    } else if (cfg.modem == MODE_FSK || cfg.modem == MODE_OOK) {
        f.tlvU32(P_BITRATE, (uint32_t)lroundf(cfg.br * 1000.0f));
        f.tlvU32(P_FDEV, (uint32_t)lroundf(cfg.fdev * 1000.0f));
        f.tlvU32(P_RXBW, (uint32_t)lroundf(cfg.rxbw * 1000.0f));
        f.tlvU8(P_SHAPING, cfg.shaping);
        f.tlv(P_SYNCBYTES, cfg.syncBytes, cfg.syncLen);
        f.tlvU16(P_FSK_PREAMBLE, cfg.fskPreamble);
        f.tlvU8(P_FSK_CRC, cfg.fskCrc ? 1 : 0);
        f.tlvU8(P_FIXED_LEN, cfg.fixedLen);
    } else {
        f.tlvU8(P_LR_BW, cfg.lrBw);
        f.tlvU8(P_LR_CR, cfg.lrCr);
        f.tlvU8(P_LR_GRID, cfg.lrNarrowGrid ? 1 : 0);
    }
    f.send(io);
}

static void sendInfo()
{
    FrameWriter f(MSG_INFO);
    f.tlvStr(I_FW, FW_VERSION);
    f.tlvStr(I_BOARD, MODEM_BOARD);
    f.tlvStr(I_RADIO, RADIO_NAME);
    f.tlvU16(I_MAX_PAYLOAD, MAX_PAYLOAD);
    f.tlvU16(I_BATT_MV, (uint16_t)lroundf(boardBatteryVoltage() * 1000.0f));
    f.tlvU32(I_UPTIME_S, millis() / 1000);
    f.tlvStr(I_POWER_PATH, powerPath);
    f.tlvStr(I_GPS, gpsStatus());
    f.tlvStr(I_BLE, !HAS_BLE ? "none"
                             : (bleConnected() ? "connected"
                                               : (bleAdvertising() ? "advertising" : "off")));
    f.tlvStr(I_BLE_NAME, bleName());
    f.send(io);
}

// ---------------------------------------------------------------- config set

// Applies one parameter. Returns false if the id is unknown or out of range.
static bool applyParam(uint8_t id, const uint8_t *v, uint8_t len)
{
    uint32_t n = TlvReader::toU32(v, len);

    switch (id) {
        case P_MODEM:
            if (n > MODE_LRFHSS) return false;
#if !HAS_OOK
            if (n == MODE_OOK) return false;
#endif
#ifndef ENABLE_LRFHSS
            if (n == MODE_LRFHSS) return false;
#endif
            cfg.modem = (uint8_t)n;
            return true;
        case P_FREQ:     cfg.freq = n / 1000000.0f; return true;
        case P_POWER: {
            int8_t p = (int8_t)v[0];
            if (p < PWR_MIN || p > PWR_MAX) return false;
            cfg.power = p;
            return true;
        }
        case P_REG_LDO:  cfg.regLdo = (n != 0); return true;
        case P_BW:       cfg.bw = n / 1000.0f; return true;
        case P_SF:       cfg.sf = (uint8_t)n; return true;
        case P_CR:       cfg.cr = (uint8_t)n; return true;
        case P_SYNCWORD: cfg.syncWord = (uint8_t)n; return true;
        case P_PREAMBLE: cfg.preamble = (uint16_t)n; return true;
        case P_CRC:      cfg.crc = (n != 0); return true;
        case P_IMPLICIT:
            if (n > MAX_PAYLOAD) return false;
            cfg.loraImplicit = (uint8_t)n;
            return true;
        case P_CR_LI:
#if !RADIO_IS_SX128X
            if (n != 0) return false;   // an SX127x/SX126x has no such rate
#endif
            cfg.crLongInterleave = (n != 0);
            return true;
        case P_BITRATE:  cfg.br = n / 1000.0f; return true;
        case P_FDEV:     cfg.fdev = n / 1000.0f; return true;
        case P_RXBW:     cfg.rxbw = n / 1000.0f; return true;
        case P_SHAPING:  cfg.shaping = (uint8_t)n; return true;
        case P_SYNCBYTES:
            if (len > MAX_SYNC) return false;
            memcpy(cfg.syncBytes, v, len);
            cfg.syncLen = len;
            return true;
        case P_FSK_PREAMBLE: cfg.fskPreamble = (uint16_t)n; return true;
        case P_FSK_CRC:  cfg.fskCrc = (n != 0); return true;
        case P_LR_BW:    cfg.lrBw = (uint8_t)n; return true;
        case P_LR_CR:    cfg.lrCr = (uint8_t)n; return true;
        case P_LR_GRID:  cfg.lrNarrowGrid = (n != 0); return true;
        case P_FIXED_LEN: cfg.fixedLen = (uint8_t)n; return true;
        default:         return false;
    }
}

// Copies a length-delimited TLV string into a fixed buffer, always terminated.
static void copyField(char *dst, size_t cap, const uint8_t *src, uint8_t len)
{
    size_t n = len < cap - 1 ? len : cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// ---------------------------------------------------------------- dispatch

static void handleFrame(uint8_t type, const uint8_t *val, uint16_t len)
{
    switch (type) {
        case MSG_PING:
            sendAck();
            break;

        case MSG_GET_INFO:
            sendInfo();
            break;

        case MSG_GET_CONFIG:
            sendConfig();
            break;

        case MSG_SET_CONFIG: {
            // Whole set is applied, then the radio is reconfigured once.
            Config saved = cfg;
            TlvReader r(val, len);
            uint8_t id, plen;
            const uint8_t *pval;
            while (r.next(&id, &pval, &plen)) {
                if (!applyParam(id, pval, plen)) {
                    cfg = saved;
                    fail(E_BAD_PARAM, id);
                    return;
                }
            }
            if (reconfigure()) {
                sendConfig();
                displayStatus(MODEM_BOARD, RADIO_NAME, FW_VERSION,
                              modeName(cfg.modem), cfg.freq, cfg.power);
            } else {
                cfg = saved;
            }
            break;
        }

        case MSG_TX:
            doTransmit(val, len);
            break;

        case MSG_CW: {
            if (txActive) { fail(E_BUSY, 0); return; }
            uint16_t secs = len >= 2 ? (uint16_t)(val[0] | (val[1] << 8)) : 5;
            if (secs == 0 || secs > 60) { fail(E_RANGE, (int16_t)secs); return; }
            busy = true;
            ledWrite(LED_RX, false);
            int16_t s = radio.transmitDirect();
            if (s != RADIOLIB_ERR_NONE) { busy = false; fail(E_RADIO, s); return; }
            ledWrite(LED_TX, true);
            sendAck();
            delay((uint32_t)secs * 1000);
            radio.standby();
            ledWrite(LED_TX, false);
            busy = false;
            startRx();
            FrameWriter f(MSG_TX_DONE);
            f.u16(0);
            f.u32((uint32_t)secs * 1000);
            f.send(io);
            break;
        }

        case MSG_RX_ENABLE:
            if (len < 1) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            if (val[0] && !rxCapable) { fail(E_TX_ONLY, 0); return; }
            rxEnabled = (val[0] != 0);
            if (rxEnabled) startRx(); else radio.standby();
            sendAck();
            break;

        case MSG_SAVE:
            saveConfig();
            sendAck();
            break;

        case MSG_LOAD:
            loadConfig();
            gpsFeed(cfg.gpsFeed);
            bleEnable(cfg.bleOn);
            reconfigure();
            sendConfig();
            break;

        case MSG_GPS:
            if (len < 1) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            if (!HAS_GPS) { fail(E_UNSUPPORTED, 0); return; }
            cfg.gpsFeed = (val[0] != 0);
            gpsFeed(cfg.gpsFeed);
            sendAck();
            break;

        case MSG_BLE:
            if (len < 1) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            if (!HAS_BLE) { fail(E_UNSUPPORTED, 0); return; }
            cfg.bleOn = (val[0] != 0);
            // The ack has to leave before the link it arrived on goes away.
            sendAck();
            io.flush();
            bleEnable(cfg.bleOn);
            break;

        case MSG_GET_STATS: {
            FrameWriter f(MSG_STATS);
            f.u32(txCount);
            f.u32(rxCount);
            f.u32(errCount);
            f.send(io);
            break;
        }

        case MSG_DIAG: {
            FrameWriter f(MSG_DIAG_RESULT);
#if RADIO_IS_SX126X
            f.u8(radio.diagOcp());          // OCP register, 2.5 mA per step
            f.u16(radio.diagErrors());
#else
            f.u8(0);
            f.u16(0);
#endif
            f.send(io);
            break;
        }

        case MSG_SCAN: {
            // start Hz, stop Hz, step Hz, dwell ms. Reports peak RSSI per step,
            // in tenths of a dBm, so a host can find activity without an SDR.
            if (txActive) { fail(E_BUSY, 0); return; }
            if (len < 14) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            uint32_t start = TlvReader::toU32(val, 4);
            uint32_t stop  = TlvReader::toU32(val + 4, 4);
            uint32_t step  = TlvReader::toU32(val + 8, 4);
            uint16_t dwell = (uint16_t)(val[12] | (val[13] << 8));
            if (!step || stop < start || dwell == 0 || dwell > 1000) {
                fail(E_RANGE, 0);
                return;
            }
            uint32_t steps = (stop - start) / step + 1;
            if (steps > 240) { fail(E_RANGE, (int16_t)steps); return; }

            busy = true;
            ledWrite(LED_RX, true);
            FrameWriter f(MSG_SCAN_RESULT);
            f.u32(start);
            f.u32(step);
            f.u16((uint16_t)steps);
            for (uint32_t i = 0; i < steps; i++) {
                radio.standby();
                radio.setFrequency((start + i * step) / 1000000.0f);
                radio.startReceive();
                float peak = -200.0f;
                uint32_t end = millis() + dwell;
                while (millis() < end) {
                    float r = radio.getRSSI(false);
                    if (r > peak) peak = r;
                }
                f.u16((int16_t)lroundf(peak * 10.0f));
            }
            radio.standby();
            busy = false;
            f.send(io);
            radioInit();
            startRx();
            break;
        }

        case MSG_LED: {
            if (len < 1) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            ledWrite(LED_RED,   val[0] & 0x01);
            ledWrite(LED_GREEN, val[0] & 0x02);
            ledWrite(LED_BLUE,  val[0] & 0x04);
            sendAck();
            break;
        }

        case MSG_PROTO: {
            if (txActive || busy) { fail(E_BUSY, 0); return; }
            ProtoRequest req;
            protoDefaults(&req, cfg.freq);

            TlvReader r(val, len);
            uint8_t id, plen;
            const uint8_t *pval;
            while (r.next(&id, &pval, &plen)) {
                uint32_t n = TlvReader::toU32(pval, plen);
                switch (id) {
                    case K_KIND:     req.kind = (uint8_t)n; break;
                    case K_ADDR:     req.addr = n; break;
                    case K_RATE:     req.rate = (uint16_t)n; break;
                    case K_SHIFT:    req.shiftHz = n; break;
                    case K_SRCSSID:  req.srcSsid = (uint8_t)n; break;
                    case K_DSTSSID:  req.dstSsid = (uint8_t)n; break;
                    case K_SYMBOL:   req.symbol = (uint8_t)n; break;
                    case K_ENCODING: req.encoding = (uint8_t)n; break;
                    case K_GPS:
                        if (!n) break;
                        if (!gpsPosition(req.lat, sizeof(req.lat),
                                         req.lon, sizeof(req.lon))) {
                            fail(E_NO_FIX, 0);
                            return;
                        }
                        break;
                    case K_TEXT: {
                        // Binary safe: the TLV length is authoritative, and the
                        // spare byte keeps the text-only modes NUL terminated.
                        uint16_t n = plen < sizeof(req.data) - 1 ? plen
                                                                 : sizeof(req.data) - 1;
                        memcpy(req.data, pval, n);
                        req.data[n] = 0;
                        req.dataLen = n;
                        break;
                    }
                    case K_SRC:  copyField(req.src,  sizeof(req.src),  pval, plen); break;
                    case K_DST:  copyField(req.dst,  sizeof(req.dst),  pval, plen); break;
                    case K_LAT:  copyField(req.lat,  sizeof(req.lat),  pval, plen); break;
                    case K_LON:  copyField(req.lon,  sizeof(req.lon),  pval, plen); break;
                    default: fail(E_BAD_PARAM, id); return;
                }
            }
            if (req.kind == 0) { fail(E_BAD_PARAM, K_KIND); return; }

            // An AX.25 frame longer than the FSK payload limit is transmitted
            // truncated and zero padded, which decodes as a corrupt frame
            // rather than failing visibly. Refuse it instead.
            size_t framed = protoFramedLen(&req);
            if (framed > MAX_FSK_PAYLOAD) { fail(E_BAD_LENGTH, (int16_t)framed); return; }

            // Every one of these clients drives the radio in direct mode, which
            // the SX126x only offers from FSK; asking while in LoRa returns
            // RADIOLIB_ERR_WRONG_MODEM.
            Config saved = cfg;
            cfg.modem = MODE_FSK;
            if (!radioInit()) { cfg = saved; radioInit(); startRx(); return; }

            busy = true;
            ledWrite(LED_RX, false);
            ledWrite(LED_TX, true);
            uint32_t t0 = millis();
            int16_t s = protoSend(&req);
            uint32_t dt = millis() - t0;
            ledWrite(LED_TX, false);
            busy = false;

            // Every client leaves the radio in direct mode, so rebuild the
            // packet configuration from scratch.
            cfg = saved;
            radioInit();
            startRx();
            rxFlag = false;

            if (s == RADIOLIB_ERR_NONE) {
                txCount++;
                FrameWriter f(MSG_TX_DONE);
                f.u16(0);
                f.u32(dt);
                f.send(io);
            } else {
                fail(E_RADIO, s);
            }
            break;
        }

        case MSG_PIN: {
            if (len < 2) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            pinMode(val[0], OUTPUT);
            digitalWrite(val[0], val[1] ? HIGH : LOW);
            sendAck();
            break;
        }

        case MSG_RESET: {
            bool toBootloader = len >= 1 && val[0] != 0;
            sendAck();
            io.flush();
            delay(50);
            if (toBootloader && halBootloader()) break;
            halReboot();
            break;
        }

        default:
            fail(E_UNKNOWN_MSG, type);
            break;
    }
}

// ---------------------------------------------------------------- framing

// src/link.cpp owns the frame parsers, one per transport, and calls back here.
void linkError(uint8_t reason, int16_t code)
{
    fail(reason, code);
}

// ---------------------------------------------------------------- lifecycle

void setup()
{
    halSerialBegin(MODEM_SERIAL_BAUD);
    delay(200);

    ledInit();
    powerPath = boardPowerInit();
    gpsInit();
    linkInit();
    displayInit();
#if defined(BOARD_NUCLEO_WL55)
    radio.setRfSwitchTable(rfswitchPins, rfswitchTable);   // must precede begin()
#elif defined(BOARD_TECHO) || defined(BOARD_RP2)
    // T-Echo: the variant already pins SPI to the radio bus. RP2: the ESP8266
    // core has one hardware SPI on fixed pins, which are the radio's.
    SPI.begin();
#else
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
#endif
    loadConfig();

    if (!radioInit()) {
#if RADIO_IS_SX128X
        uint8_t ver[16] = { 0 };
        radio.diagVersion(ver);
        outf("sx128x id: %02x%02x%02x%02x%02x%02x%02x%02x '%c%c%c%c%c%c'\n",
             ver[0], ver[1], ver[2], ver[3], ver[4], ver[5], ver[6], ver[7],
             isprint(ver[0]) ? ver[0] : '.', isprint(ver[1]) ? ver[1] : '.',
             isprint(ver[2]) ? ver[2] : '.', isprint(ver[3]) ? ver[3] : '.',
             isprint(ver[4]) ? ver[4] : '.', isprint(ver[5]) ? ver[5] : '.');
#endif
        while (true) {
            fail(E_RADIO, 0);
            delay(2000);
        }
    }
    startRx();
    gpsFeed(cfg.gpsFeed);
    bleEnable(cfg.bleOn);

    displayStatus(MODEM_BOARD, RADIO_NAME, FW_VERSION,
                  modeName(cfg.modem), cfg.freq, cfg.power);

    FrameWriter f(MSG_READY);
    f.send(io);
    sendInfo();
}

void loop()
{
    linkPoll(handleFrame);
    gpsPoll(sendNmea);

    if (txActive) {
        if (rxFlag) finishTransmit();
        // A stuck transmit must not wedge the modem: SF12 at 255 bytes is
        // roughly 8 s, so anything past 15 s is a failure, not slow airtime.
        else if (millis() - txStart > 15000) {
            ledWrite(LED_TX, false);
            txActive = false;
            fail(E_RADIO, RADIOLIB_ERR_TX_TIMEOUT);
            startRx();
        }
    } else {
        drainRx();
        // Repainting e-paper blocks for seconds, so never during a transmit.
        displayPoll();
    }
}
