#pragma once
#include <Arduino.h>

#if defined(BOARD_TBEAM)

// T-Beam v0.7 .. v1.2 share this SPI bus for the radio.
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_CS    18
#define LORA_RST   23
#define LORA_DIO0  26  // SX1276 only
#define LORA_DIO1  33
#define LORA_BUSY  32  // SX1262 only
#define LORA_TCXO_V 1.6f

#define OLED_ADDR_A 0x3C   // SSD1306, some modules answer at 0x3D
#define OLED_ADDR_B 0x3D

#define PMU_SDA    21
#define PMU_SCL    22
#define PMU_IRQ    35

#define GPS_RX     34  // ESP32 receives on this
#define GPS_TX     12

#ifdef MODEM_USE_UART2
#define MODEM_UART2_TX 25
#define MODEM_UART2_RX 14
#endif

#define DEFAULT_FREQ 868.0f
#define LED_RED    4       // the single onboard LED, used for every state
#define LED_GREEN  4
#define LED_BLUE   4
#define LED_RX     LED_RED
#define LED_TX     LED_BLUE
#define LED_ACTIVE_LOW 0

#define MODEM_BOARD "T-Beam"

#elif defined(BOARD_TECHO)

// LilyGO T-Echo, nRF52840 + SX1262. Arduino pin numbers are an identity map onto
// the nRF port pins, so (32 + n) is P1.n.
#define LORA_SCK   (0 + 19)
#define LORA_MISO  (0 + 23)
#define LORA_MOSI  (0 + 22)
#define LORA_CS    (0 + 24)
#define LORA_RST   (0 + 25)
#define LORA_DIO1  (0 + 20)
#define LORA_BUSY  (0 + 17)
#define LORA_TCXO_V 1.8f   // DIO3 drives the TCXO at 1.8 V on this board

// One rail feeds e-paper, GPS, LoRa and the sensor. Nothing works until it is high.
#define PIN_POWER_EN (0 + 12)

// 1.54" e-paper, on its own SPI bus rather than the radio's.
#define PIN_EINK_EN    (32 + 11)
#define PIN_EINK_CS    (0 + 30)
#define PIN_EINK_BUSY  (0 + 3)
#define PIN_EINK_DC    (0 + 28)
#define PIN_EINK_RES   (0 + 2)
#define PIN_EINK_SCLK  (0 + 31)
#define PIN_EINK_MOSI  (0 + 29)
#define EINK_MISO_UNUSED (32 + 7)   // the SPI block needs a pin, nothing is wired

#define BATT_ADC    PIN_A0
#define BATT_DIVIDER 2.0f

#define GPS_RX     (32 + 9)
#define GPS_TX     (32 + 8)

#define DEFAULT_FREQ 868.0f
// One RGB package, common anode, so all three sink through the MCU. Meshtastic's
// variant comments disagree with its own LED_RED/LED_BLUE defines; the defines
// are what its code uses and match the hardware.
#define LED_RED    (0 + 13)
#define LED_BLUE   (0 + 14)
#define LED_GREEN  (0 + 15)
#define LED_RX     LED_RED
#define LED_TX     LED_BLUE
#define LED_ACTIVE_LOW 1

#define MODEM_BOARD "T-Echo"

#elif defined(BOARD_NUCLEO_WL55)

// ST NUCLEO-WL55JC. The radio is a die-integrated SX126x core, so there is no
// SPI bus and no CS/RST/BUSY pins to name. What the board does need is the
// front-end switch table, wired to PC3/PC4/PC5 on this Nucleo.
//
// Two variants exist and the sticker on the RF shield is the only way to tell:
// the JC1 is matched for 865-928 MHz, the JC2 for 430-510 MHz. Running a JC2 at
// 868 costs 30-40 dB in the matching network while the chip reports no error at
// all, so build with -DWL55_LOW_BAND for a JC2.
#define LORA_TCXO_V 1.7f

// UM2592 6.6.3: the high-power PA needs REG PA fed directly from VDDSMPS, so
// the DC-DC path is required to reach 22 dBm. The low-power PA runs off the
// regulated VFBSMPS rail instead.
#define DEFAULT_REG_LDO 0

#ifdef WL55_LOW_BAND
#define DEFAULT_FREQ 434.0f
#else
#define DEFAULT_FREQ 868.0f
#endif
#define WL_RFSW_1  PC3
#define WL_RFSW_2  PC4
#define WL_RFSW_3  PC5

#define LED_RED    PB11       // LED3
#define LED_GREEN  PB9        // LED2
#define LED_BLUE   PB15       // LED1
#define LED_RX     LED_RED
#define LED_TX     LED_BLUE
#define LED_ACTIVE_LOW 0

#define MODEM_BOARD "Nucleo-WL55JC"

#else
#error "Build with -DBOARD_TBEAM, -DBOARD_TECHO or -DBOARD_NUCLEO_WL55"
#endif

// Brings up whatever rail feeds the radio. Returns a short description of the
// power path, used in the boot banner and in the fatal-init message.
const char *boardPowerInit();
float boardBatteryVoltage();
