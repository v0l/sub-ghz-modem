#ifdef BOARD_TECHO

#include "board.h"

const char *boardPowerInit()
{
    // PIN_POWER_EN gates the e-paper, GPS, LoRa and sensor rail. Without it the
    // SX1262 never answers on SPI, which looks exactly like a wiring fault.
    pinMode(PIN_POWER_EN, OUTPUT);
    digitalWrite(PIN_POWER_EN, HIGH);
    delay(10);

    analogReference(AR_INTERNAL_3_0);
    analogReadResolution(12);

    return "gpio-rail";
}

float boardBatteryVoltage()
{
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) raw += analogRead(BATT_ADC);
    return (raw / 8.0f) * (3.0f / 4096.0f) * BATT_DIVIDER;
}

#endif // BOARD_TECHO
