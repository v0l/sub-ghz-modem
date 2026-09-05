#ifdef BOARD_TECHO

#include "hal.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define CFG_PATH "/modem.cfg"

Stream &io = Serial;

void halSerialBegin(unsigned long baud)
{
    Serial.begin(baud);   // USB CDC, the baud rate is cosmetic
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);
}

void halReboot()
{
    NVIC_SystemReset();
}

bool halSettingsLoad(void *blob, size_t len)
{
    if (!InternalFS.begin()) return false;
    File f(InternalFS);
    if (!f.open(CFG_PATH, FILE_O_READ)) return false;
    int got = f.read(blob, len);
    f.close();
    return got == (int)len;
}

void halSettingsSave(const void *blob, size_t len)
{
    if (!InternalFS.begin()) return;
    InternalFS.remove(CFG_PATH);
    File f(InternalFS);
    if (!f.open(CFG_PATH, FILE_O_WRITE)) return;
    f.write((const uint8_t *)blob, len);
    f.close();
}

#endif // BOARD_TECHO
