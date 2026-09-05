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
#else
#error "Build with -DRADIO_SX1276, -DRADIO_SX1262 or -DRADIO_STM32WLX"
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
};

#if defined(RADIO_STM32WLX)
static ModemRadio radio(new STM32WLx_Module());
#elif defined(RADIO_SX1262)
static ModemRadio radio(new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY));
#else
static ModemRadio radio(new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1));
#endif

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

#define FW_VERSION   "1.2.0"
#define CFG_MAGIC    0x4D475A53UL   // "SZGM"
#define CFG_VERSION  6
#define MAX_PAYLOAD  255
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
};

static const Config defaults = {
    CFG_MAGIC, CFG_VERSION,
    MODE_LORA, DEFAULT_FREQ, 17,
    125.0f, 9, 7, 0x12, 8, true,
    4.8f, 5.0f, 156.2f, RADIOLIB_SHAPING_NONE, 2, {0x2D, 0xD4, 0, 0, 0, 0, 0, 0}, 16, true, 0,
    0, 0, false,
#ifdef DEFAULT_REG_LDO
    DEFAULT_REG_LDO,
#else
    false,
#endif
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
        if ((s = radio.setCRC(cfg.crc)) != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

    } else if (cfg.modem == MODE_FSK || cfg.modem == MODE_OOK) {
#if RADIO_IS_SX126X
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
        else              s = radio.variablePacketLengthMode(MAX_PAYLOAD);
        if (s != RADIOLIB_ERR_NONE) { fail(E_RADIO, s); return false; }

        if ((s = radio.setDataShaping(cfg.shaping)) != RADIOLIB_ERR_NONE) {
            fail(E_RADIO, s); return false;
        }
        if (cfg.syncLen > 0) {
            if ((s = radio.setSyncWord(cfg.syncBytes, cfg.syncLen)) != RADIOLIB_ERR_NONE) {
                fail(E_RADIO, s); return false;
            }
        }
#if RADIO_IS_SX126X
        // On SX126x the FSK setCRC argument is a length in bytes, not a flag.
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
    if (len == 0 || len > MAX_PAYLOAD) { fail(E_BAD_LENGTH, (int16_t)len); return; }
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
    f.tlvU32(P_FREQ, (uint32_t)lroundf(cfg.freq * 1000000.0f));
    f.tlvU8(P_POWER, (uint8_t)cfg.power);
    f.tlvU8(P_REG_LDO, cfg.regLdo ? 1 : 0);

    if (cfg.modem == MODE_LORA) {
        f.tlvU32(P_BW, (uint32_t)lroundf(cfg.bw * 1000.0f));
        f.tlvU8(P_SF, cfg.sf);
        f.tlvU8(P_CR, cfg.cr);
        f.tlvU8(P_SYNCWORD, cfg.syncWord);
        f.tlvU16(P_PREAMBLE, cfg.preamble);
        f.tlvU8(P_CRC, cfg.crc ? 1 : 0);
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
            reconfigure();
            sendConfig();
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

        case MSG_PIN: {
            if (len < 2) { fail(E_BAD_LENGTH, (int16_t)len); return; }
            pinMode(val[0], OUTPUT);
            digitalWrite(val[0], val[1] ? HIGH : LOW);
            sendAck();
            break;
        }

        case MSG_RESET:
            sendAck();
            io.flush();
            delay(50);
            halReboot();
            break;

        default:
            fail(E_UNKNOWN_MSG, type);
            break;
    }
}

// ---------------------------------------------------------------- framing

static void pollSerial()
{
    static uint8_t state = 0, type = 0;
    static uint16_t want = 0, got = 0, crcGot = 0;
    static uint8_t val[PROTO_MAX_VALUE];

    while (io.available()) {
        uint8_t c = (uint8_t)io.read();
        switch (state) {
            case 0: if (c == PROTO_SOF0) state = 1; break;
            // A second SOF0 keeps us waiting rather than dropping a real frame
            // that follows a stray byte.
            case 1: state = (c == PROTO_SOF1) ? 2 : (c == PROTO_SOF0 ? 1 : 0); break;
            case 2: type = c; state = 3; break;
            case 3: want = c; state = 4; break;
            case 4:
                want |= (uint16_t)c << 8;
                if (want > PROTO_MAX_VALUE) { fail(E_BAD_LENGTH, (int16_t)want); state = 0; break; }
                got = 0;
                state = want ? 5 : 6;
                break;
            case 5:
                val[got++] = c;
                if (got >= want) state = 6;
                break;
            case 6: crcGot = c; state = 7; break;
            case 7: {
                crcGot |= (uint16_t)c << 8;
                uint8_t head[3] = { type, (uint8_t)(want & 0xFF), (uint8_t)(want >> 8) };
                uint16_t crc = protoCrc16(head, 3);
                // Continue the CRC across the value with the same seed.
                for (uint16_t i = 0; i < want; i++) {
                    crc ^= (uint16_t)val[i] << 8;
                    for (int b = 0; b < 8; b++)
                        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
                }
                if (crc == crcGot) handleFrame(type, val, want);
                else fail(E_BAD_CRC, (int16_t)crcGot);
                state = 0;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- lifecycle

void setup()
{
    halSerialBegin(MODEM_SERIAL_BAUD);
    delay(200);

    ledInit();
    powerPath = boardPowerInit();
    displayInit();
#if defined(BOARD_NUCLEO_WL55)
    radio.setRfSwitchTable(rfswitchPins, rfswitchTable);   // must precede begin()
#elif defined(BOARD_TECHO)
    SPI.begin();   // the variant already pins SPI to the radio bus
#else
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
#endif
    loadConfig();

    if (!radioInit()) {
        while (true) {
            fail(E_RADIO, 0);
            delay(2000);
        }
    }
    startRx();

    displayStatus(MODEM_BOARD, RADIO_NAME, FW_VERSION,
                  modeName(cfg.modem), cfg.freq, cfg.power);

    FrameWriter f(MSG_READY);
    f.send(io);
    sendInfo();
}

void loop()
{
    pollSerial();
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
    }
}
