// Minimal Adafruit-nRF52-BSP variant for the LilyGO T-Echo.
// Only the peripherals this firmware touches are described: radio SPI, I2C,
// the GPS UART, the LEDs, the buttons and the battery ADC. The e-paper display
// and its SPI1 bus are deliberately left out.

#ifndef _VARIANT_T_ECHO_
#define _VARIANT_T_ECHO_

#include <stdint.h>

#define _PINNUM(port, pin) ((port) * 32 + (pin))

#ifdef __cplusplus
extern "C" {
#endif

// The T-Echo fits a 32.768 kHz crystal (X2), so use the low-frequency XO.
#define USE_LFXO

#define PINS_COUNT          (48)
#define NUM_DIGITAL_PINS    (48)
#define NUM_ANALOG_INPUTS   (6)
#define NUM_ANALOG_OUTPUTS  (0)

// LEDs, all active low
#define PIN_LED1     (0 + 14)   // red
#define PIN_LED2     (0 + 15)   // blue
#define PIN_LED3     (0 + 13)   // green
#define LED_BUILTIN  PIN_LED1
#define LED_STATE_ON 0

// Buttons. P0.18 is silkscreened RESET but the bootloader configures it as GPIO.
#define PIN_BUTTON1        (32 + 10)
#define PIN_BUTTON2        (0 + 18)
#define PIN_BUTTON_TOUCH   (0 + 11)

// Analog. A0 is the battery divider.
#define PIN_A0 (4)
#define PIN_A1 (5)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
#define ADC_RESOLUTION 14
#define AREF_VOLTAGE   3.0

// NFC pins are used as GPIO on this board
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

// Serial1 goes to the L76K GPS
#define PIN_SERIAL1_RX (32 + 9)
#define PIN_SERIAL1_TX (32 + 8)

// SPI: the SX1262
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (0 + 23)
#define PIN_SPI_MOSI (0 + 22)
#define PIN_SPI_SCK  (0 + 19)
static const uint8_t SS   = (0 + 24);
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

// I2C: PCF8563 RTC and the sensor
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (26)
#define PIN_WIRE_SCL (27)
static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

#ifdef __cplusplus
}
#endif

#endif // _VARIANT_T_ECHO_
