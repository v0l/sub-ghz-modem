#ifdef BOARD_RP2

#include "board.h"

const char *boardPowerInit()
{
    // Nothing to switch. The 5 V pad feeds an on-board LDO that supplies the
    // ESP8285 and the SX1281 together, and neither has an enable line.
    return "5V pad, on-board LDO";
}

float boardBatteryVoltage()
{
    return 0.0f;   // no cell, no divider
}

#endif // BOARD_RP2
