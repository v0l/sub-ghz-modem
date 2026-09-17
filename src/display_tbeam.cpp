#include "display.h"

#if HAS_DISPLAY && defined(BOARD_TBEAM)

#include "board.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool ready = false;

// 8x8 lamps, one bit per pixel, most significant bit at the left. A hollow
// glyph is a link that is up but idle and a filled one is a link carrying
// something, so the row reads at a glance across the bottom of the screen.
static const uint8_t ICON_BT[] PROGMEM = {
    0x10, 0x18, 0x54, 0x38, 0x38, 0x54, 0x18, 0x10,
};
static const uint8_t ICON_GPS[] PROGMEM = {
    0x3C, 0x42, 0x99, 0xBD, 0xBD, 0x99, 0x42, 0x3C,
};
static const uint8_t ICON_GPS_FIX[] PROGMEM = {
    0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C,
};
static const uint8_t ICON_SERIAL[] PROGMEM = {
    0x18, 0x18, 0x7E, 0x5A, 0x7E, 0x3C, 0x3C, 0x18,
};
static const uint8_t ICON_TX[] PROGMEM = {
    0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18,
};
static const uint8_t ICON_RX[] PROGMEM = {
    0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18,
};

static bool probe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void displayInit()
{
    // The PMU already started Wire on this board. A T-Beam without the optional
    // OLED must still boot, so probe rather than assume.
    uint8_t addr = probe(OLED_ADDR_A) ? OLED_ADDR_A
                 : probe(OLED_ADDR_B) ? OLED_ADDR_B : 0;
    if (!addr) return;

    ready = oled.begin(SSD1306_SWITCHCAPVCC, addr);
    if (ready) {
        oled.clearDisplay();
        oled.display();
    }
}

// Right to left along the bottom edge, so a lamp that is off closes the gap
// rather than leaving a hole where something was.
static void lampRow(const Lamps &l)
{
    int x = 120;
    auto icon = [&](const uint8_t *bm, bool boxed) {
        oled.drawBitmap(x, 56, bm, 8, 8, SSD1306_WHITE);
        if (boxed) oled.drawRect(x - 1, 55, 10, 9, SSD1306_WHITE);
        x -= 11;
    };

    if (l.tx) icon(ICON_TX, false);
    else if (l.rx) icon(ICON_RX, false);
    if (l.serial) icon(ICON_SERIAL, false);
    if (l.gps) icon(l.gpsFix ? ICON_GPS_FIX : ICON_GPS, false);
    // A box round the rune is a host on the other end of it; the bare rune is
    // advertising to nobody.
    if (l.bleLink || l.bleAdv) icon(ICON_BT, l.bleLink);
}

void displayRender(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power,
                   const Lamps &lamps)
{
    if (!ready) return;

    uint32_t khz = (uint32_t)lroundf(freqMhz * 1000.0f);

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("sub-ghz-modem");

    int pct = boardBatteryPercent();
    if (pct >= 0) {
        char batt[8];
        snprintf(batt, sizeof(batt), "%d%%", pct);
        oled.setCursor(127 - 6 * (int)strlen(batt), 0);
        oled.print(batt);
    }

    oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    oled.setCursor(0, 16);
    oled.printf("%s %s", board, radio);
    oled.setCursor(0, 26);
    oled.printf("fw %s", fw);

    oled.setTextSize(2);
    oled.setCursor(0, 38);
    oled.printf("%lu.%03lu", (unsigned long)(khz / 1000), (unsigned long)(khz % 1000));

    oled.setTextSize(1);
    oled.setCursor(0, 56);
    oled.printf("%s %d", modem, power);

    lampRow(lamps);

    oled.display();
}

#endif
