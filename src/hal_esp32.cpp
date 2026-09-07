#ifdef BOARD_TBEAM

#include "hal.h"
#include "board.h"
#include <Preferences.h>

Stream &halPort()
{
#ifdef MODEM_USE_UART2
    return Serial2;
#else
    return Serial;
#endif
}

void halSerialBegin(unsigned long baud)
{
#ifdef MODEM_USE_UART2
    Serial.begin(115200);   // USB stays a plain log port
    Serial2.begin(baud, SERIAL_8N1, MODEM_UART2_RX, MODEM_UART2_TX);
#else
    Serial.begin(baud);
#endif
}

void halReboot()
{
    ESP.restart();
}

bool halBootloader()
{
    return false;   // flashed over the serial bootloader, no host-visible mode
}

bool halSettingsLoad(void *blob, size_t len)
{
    Preferences prefs;
    if (!prefs.begin("modem", true)) return false;
    size_t got = prefs.getBytes("blob", blob, len);
    prefs.end();
    return got == len;
}

void halSettingsSave(const void *blob, size_t len)
{
    Preferences prefs;
    if (!prefs.begin("modem", false)) return;
    prefs.putBytes("blob", blob, len);
    prefs.end();
}

#endif // BOARD_TBEAM
