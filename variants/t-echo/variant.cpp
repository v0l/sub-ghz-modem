#include "Arduino.h"
#include "variant.h"

// Identity map: Arduino pin n is nRF pin n, so P0.x is x and P1.x is 32 + x.
const uint32_t g_ADigitalPinMap[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
};

void initVariant()
{
    pinMode(PIN_LED1, OUTPUT);
    digitalWrite(PIN_LED1, !LED_STATE_ON);
}
