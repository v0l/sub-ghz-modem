#pragma once
#include <Arduino.h>

// Boards without a screen compile these away to nothing.
void displayInit();

// Redrawn at boot and whenever the configuration changes. E-paper refresh is
// slow and blocking, so nothing calls this on a per-packet basis.
void displayStatus(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power);
