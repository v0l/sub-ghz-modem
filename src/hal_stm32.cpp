#ifdef BOARD_NUCLEO_WL55

#include "hal.h"
#include <EEPROM.h>   // pulls in the core stm32_eeprom buffered API

Stream &io = Serial;   // LPUART1, the ST-LINK virtual COM port

void halSerialBegin(unsigned long baud)
{
    Serial.begin(baud);
}

void halReboot()
{
    NVIC_SystemReset();
}

// STM32WL has no real EEPROM. The core emulates one in the last flash page, so
// writes go through a RAM buffer and one page erase, not a byte at a time.
bool halSettingsLoad(void *blob, size_t len)
{
    if (len > E2END + 1) return false;
    eeprom_buffer_fill();
    uint8_t *p = (uint8_t *)blob;
    for (size_t i = 0; i < len; i++) p[i] = eeprom_buffered_read_byte(i);
    return true;
}

void halSettingsSave(const void *blob, size_t len)
{
    if (len > E2END + 1) return;
    const uint8_t *p = (const uint8_t *)blob;
    for (size_t i = 0; i < len; i++) eeprom_buffered_write_byte(i, p[i]);
    eeprom_buffer_flush();
}

#endif // BOARD_NUCLEO_WL55
