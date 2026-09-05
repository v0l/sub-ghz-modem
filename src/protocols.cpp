#include "protocols.h"
#include "radio.h"
#include <RadioLib.h>

void protoDefaults(ProtoRequest *r, float baseMhz)
{
    memset(r, 0, sizeof(*r));
    r->baseMhz = baseMhz;
    r->rate = 0;            // each kind fills in its own sensible default
    r->shiftHz = 0;
    r->symbol = '>';        // car, the least wrong default for a moving thing
    strcpy(r->src, "N0CALL");
    strcpy(r->dst, "APRS");
}

int16_t protoSend(const ProtoRequest *r)
{
    PhysicalLayer *phy = radioPhy();
    int16_t state = RADIOLIB_ERR_UNKNOWN;

    switch (r->kind) {
        case PROTO_APRS: {
            AX25Client ax25(phy);
            APRSClient aprs(&ax25);
            state = ax25.begin(r->src, r->srcSsid);
            if (state != RADIOLIB_ERR_NONE) return state;
            radioPacketMode();
            state = aprs.begin((char)r->symbol);
            if (state != RADIOLIB_ERR_NONE) return state;
            if (r->lat[0] && r->lon[0]) {
                state = aprs.sendPosition((char *)r->dst, r->dstSsid, r->lat, r->lon,
                                          r->dataLen ? (const char *)r->data : nullptr);
            } else {
                state = aprs.sendFrame((char *)r->dst, r->dstSsid, (char *)r->data);
            }
            break;
        }

        case PROTO_AX25: {
            AX25Client ax25(phy);
            state = ax25.begin(r->src, r->srcSsid);
            if (state != RADIOLIB_ERR_NONE) return state;
            radioPacketMode();
            // Build the frame by hand so the information field can hold
            // arbitrary bytes rather than a C string.
            AX25Frame frame(r->dst, r->dstSsid, r->src, r->srcSsid,
                            RADIOLIB_AX25_CONTROL_U_UNNUMBERED_INFORMATION |
                            RADIOLIB_AX25_CONTROL_POLL_FINAL_DISABLED |
                            RADIOLIB_AX25_CONTROL_UNNUMBERED_FRAME,
                            RADIOLIB_AX25_PID_NO_LAYER_3,
                            r->data, r->dataLen);
            state = ax25.sendFrame(&frame);
            break;
        }

        case PROTO_POCSAG: {
            PagerClient pager(phy);
            state = pager.begin(r->baseMhz, r->rate ? r->rate : 1200);
            if (state != RADIOLIB_ERR_NONE) return state;
            state = pager.transmit(r->data, r->dataLen, r->addr,
                                   r->encoding ? r->encoding : RADIOLIB_PAGER_ASCII);
            break;
        }

        case PROTO_RTTY: {
            RTTYClient rtty(phy);
            state = rtty.begin(r->baseMhz, r->shiftHz ? r->shiftHz : 170,
                               r->rate ? r->rate : 45, RADIOLIB_ASCII, 1);
            if (state != RADIOLIB_ERR_NONE) return state;
            rtty.idle();
            for (uint16_t i = 0; i < r->dataLen; i++) rtty.write(r->data[i]);
            phy->standby();
            state = RADIOLIB_ERR_NONE;
            break;
        }

        case PROTO_MORSE: {
            MorseClient morse(phy);
            state = morse.begin(r->baseMhz, r->rate ? r->rate : 20);
            if (state != RADIOLIB_ERR_NONE) return state;
            morse.startSignal();
            morse.print((const char *)r->data);
            phy->standby();
            state = RADIOLIB_ERR_NONE;
            break;
        }

        case PROTO_HELL: {
            HellClient hell(phy);
            state = hell.begin(r->baseMhz, r->rate ? (float)r->rate : 122.5f);
            if (state != RADIOLIB_ERR_NONE) return state;
            hell.print((const char *)r->data);
            phy->standby();
            state = RADIOLIB_ERR_NONE;
            break;
        }

        case PROTO_FSK4: {
            FSK4Client fsk4(phy);
            state = fsk4.begin(r->baseMhz, r->shiftHz ? r->shiftHz : 270,
                               r->rate ? r->rate : 100);
            if (state != RADIOLIB_ERR_NONE) return state;
            fsk4.write((uint8_t *)r->data, r->dataLen);
            phy->standby();
            state = RADIOLIB_ERR_NONE;
            break;
        }

        default:
            state = RADIOLIB_ERR_UNKNOWN;
            break;
    }
    return state;
}
