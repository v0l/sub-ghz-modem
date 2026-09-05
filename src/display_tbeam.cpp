#include "display.h"

#if HAS_DISPLAY && defined(BOARD_TBEAM)

#include "board.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool ready = false;

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

void displayStatus(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power)
{
    if (!ready) return;

    uint32_t khz = (uint32_t)lroundf(freqMhz * 1000.0f);

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("sub-ghz-modem");
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
    oled.printf("%s  %d dBm", modem, power);

    oled.display();
}

#endif
