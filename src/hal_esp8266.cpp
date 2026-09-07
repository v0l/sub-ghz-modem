#ifdef BOARD_RP2

#include "hal.h"
#include "board.h"
#include <EEPROM.h>

// The ESP8266 core emulates EEPROM in one flash sector, read whole into RAM at
// begin() and written back by commit(). 512 bytes covers the config struct with
// room to grow; growing past a sector would need a different store entirely.
#define EEPROM_LEN 512

Stream &halPort()
{
    return Serial;
}

void halSerialBegin(unsigned long baud)
{
    Serial.begin(baud);
}

void halReboot()
{
    ESP.restart();
}

bool halBootloader()
{
    // GPIO0 low at reset is the only way into the ESP8285 ROM loader, and it is
    // a pad on the board rather than anything software can reach.
    return false;
}

bool halSettingsLoad(void *blob, size_t len)
{
    if (len > EEPROM_LEN) return false;
    EEPROM.begin(EEPROM_LEN);
    uint8_t *p = (uint8_t *)blob;
    for (size_t i = 0; i < len; i++) p[i] = EEPROM.read(i);
    EEPROM.end();
    return true;
}

void halSettingsSave(const void *blob, size_t len)
{
    if (len > EEPROM_LEN) return;
    EEPROM.begin(EEPROM_LEN);
    const uint8_t *p = (const uint8_t *)blob;
    for (size_t i = 0; i < len; i++) EEPROM.write(i, p[i]);
    EEPROM.commit();
    EEPROM.end();
}

#endif // BOARD_RP2
