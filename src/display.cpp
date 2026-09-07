#include "display.h"

#if HAS_DISPLAY
#include "board.h"

// An e-paper full refresh is ~2 s of blocked CPU, so it needs a coarse step and
// a long interval. The OLED costs an I2C frame, so it can follow every point.
#ifdef BOARD_TECHO
#define BATT_STEP    5
#define BATT_PERIOD  60000UL
#else
#define BATT_STEP    1
#define BATT_PERIOD  10000UL
#endif

static struct {
    const char *board, *radio, *fw, *modem;
    float freq;
    int8_t power;
    bool valid;
} last;

static int shownPct = -2;
static uint32_t nextCheck = 0;

void displayStatus(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power)
{
    // Every caller passes string literals, so keeping the pointers is safe.
    last = { board, radio, fw, modem, freqMhz, power, true };
    shownPct = boardBatteryPercent();
    nextCheck = millis() + BATT_PERIOD;
    displayRender(board, radio, fw, modem, freqMhz, power);
}

void displayPoll()
{
    if (!last.valid || (int32_t)(millis() - nextCheck) < 0) return;
    nextCheck = millis() + BATT_PERIOD;

    int pct = boardBatteryPercent();
    if (pct == shownPct) return;
    if (shownPct >= 0 && pct >= 0 && abs(pct - shownPct) < BATT_STEP) return;

    shownPct = pct;
    displayRender(last.board, last.radio, last.fw, last.modem,
                  last.freq, last.power);
}

#endif
