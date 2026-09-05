#ifdef BOARD_TBEAM

#include "board.h"

// XPowersLib selects a chip with #if/#elif, so naming both would silently pick
// only the first. Naming none takes its #else branch, which defines all of them.
#include <XPowersLib.h>
#include <Wire.h>

static XPowersLibInterface *pmu = nullptr;

const char *boardPowerInit()
{
    Wire.begin(PMU_SDA, PMU_SCL);

    // v1.2 ships an AXP2101, everything before it an AXP192. Both answer at 0x34,
    // so probe by driver rather than by address.
    pmu = new XPowersAXP2101(Wire, PMU_SDA, PMU_SCL);
    if (pmu->init()) {
        pmu->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
        pmu->enablePowerOutput(XPOWERS_ALDO2);   // LoRa
        pmu->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
        pmu->enablePowerOutput(XPOWERS_ALDO3);   // GPS
        pmu->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
        pmu->enablePowerOutput(XPOWERS_DCDC1);
        pmu->disablePowerOutput(XPOWERS_DCDC2);
        pmu->disablePowerOutput(XPOWERS_DCDC3);
        pmu->disablePowerOutput(XPOWERS_DCDC4);
        pmu->disablePowerOutput(XPOWERS_DCDC5);
        pmu->disablePowerOutput(XPOWERS_ALDO1);
        pmu->disablePowerOutput(XPOWERS_ALDO4);
        pmu->enableBattVoltageMeasure();
        delay(50);
        return "AXP2101";
    }
    delete pmu;

    pmu = new XPowersAXP192(Wire, PMU_SDA, PMU_SCL);
    if (pmu->init()) {
        pmu->setPowerChannelVoltage(XPOWERS_LDO2, 3300);
        pmu->enablePowerOutput(XPOWERS_LDO2);    // LoRa
        pmu->setPowerChannelVoltage(XPOWERS_LDO3, 3300);
        pmu->enablePowerOutput(XPOWERS_LDO3);    // GPS
        pmu->setProtectedChannel(XPOWERS_DCDC3); // feeds the ESP32, never cut it
        pmu->enableBattVoltageMeasure();
        delay(50);
        return "AXP192";
    }
    delete pmu;
    pmu = nullptr;

    return "none"; // T-Beam v0.7: radio is hard-wired to 3V3
}

float boardBatteryVoltage()
{
    if (!pmu) return 0.0f;
    return pmu->getBattVoltage() / 1000.0f;
}

#endif // BOARD_TBEAM
