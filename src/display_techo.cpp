#include "display.h"

#if HAS_DISPLAY && defined(BOARD_TECHO)
#include "board.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

// The panel sits on its own SPI bus, separate from the radio. MISO is unused by
// the display but the SPI peripheral needs a pin, so it gets an unconnected one.
static SPIClass epdSPI(NRF_SPIM2, EINK_MISO_UNUSED, PIN_EINK_SCLK, PIN_EINK_MOSI);

static GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> epd(
    GxEPD2_154_D67(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RES, PIN_EINK_BUSY));

static bool ready = false;

void displayInit()
{
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, LOW);   // backlight rail, active low on this board

    epdSPI.begin();
    epd.epd2.selectSPI(epdSPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    epd.init(0, true, 10, false);
    epd.setRotation(3);   // ribbon at the top on the T-Echo, so rotate 180 from 1
    ready = true;
}

void displayStatus(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power)
{
    if (!ready) return;

    char freq[24], line[40];
    // %f is absent from newlib-nano, so build the frequency by hand.
    uint32_t khz = (uint32_t)lroundf(freqMhz * 1000.0f);
    snprintf(freq, sizeof(freq), "%lu.%03lu MHz",
             (unsigned long)(khz / 1000), (unsigned long)(khz % 1000));

    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);
        epd.setTextColor(GxEPD_BLACK);

        epd.setFont(&FreeMonoBold9pt7b);
        epd.setCursor(4, 20);
        epd.print("sub-ghz-modem");

        epd.drawLine(4, 26, epd.width() - 4, 26, GxEPD_BLACK);

        epd.setFont(&FreeMono9pt7b);
        int y = 46;
        snprintf(line, sizeof(line), "%s", board);
        epd.setCursor(4, y); epd.print(line); y += 18;
        snprintf(line, sizeof(line), "radio %s", radio);
        epd.setCursor(4, y); epd.print(line); y += 18;
        snprintf(line, sizeof(line), "fw    %s", fw);
        epd.setCursor(4, y); epd.print(line); y += 24;

        epd.setFont(&FreeMonoBold9pt7b);
        epd.setCursor(4, y); epd.print(freq); y += 18;
        epd.setFont(&FreeMono9pt7b);
        snprintf(line, sizeof(line), "%s  %d dBm", modem, power);
        epd.setCursor(4, y); epd.print(line); y += 24;

        epd.setCursor(4, y);
        epd.print("serial: TLV @115200");
    } while (epd.nextPage());

    epd.hibernate();
}

#endif
