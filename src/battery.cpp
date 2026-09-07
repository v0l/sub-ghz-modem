#include "board.h"

// Open-circuit voltage of a single Li-ion cell at 0..100 % in 10 % steps. The
// discharge curve is flat between 3.7 and 3.9 V, so a linear voltage-to-percent
// map would report half the pack as "50 %" and then fall off a cliff.
static const uint16_t kCurveMv[11] = {
    3270, 3610, 3690, 3710, 3730, 3750, 3770, 3790, 3880, 3970, 4100,
};

int boardBatteryPercent()
{
    float v = boardBatteryVoltage();
    if (v < 2.0f) return -1;   // no battery path, or nothing plugged in

    uint16_t mv = (uint16_t)lroundf(v * 1000.0f);
    if (mv <= kCurveMv[0]) return 0;
    if (mv >= kCurveMv[10]) return 100;

    for (int i = 1; i <= 10; i++) {
        if (mv < kCurveMv[i]) {
            uint16_t lo = kCurveMv[i - 1], hi = kCurveMv[i];
            return (i - 1) * 10 + (int)lroundf(10.0f * (mv - lo) / (hi - lo));
        }
    }
    return 100;
}
