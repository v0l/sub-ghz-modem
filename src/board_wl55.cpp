#ifdef BOARD_NUCLEO_WL55

#include "board.h"

const char *boardPowerInit()
{
    // Nothing to switch on. The radio shares the MCU die and its supply, and the
    // RF switch table is applied by main.cpp before begin().
    return "internal";
}

float boardBatteryVoltage()
{
    return 0.0f;   // no battery path on the Nucleo
}

#endif // BOARD_NUCLEO_WL55
