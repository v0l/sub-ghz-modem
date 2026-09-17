#pragma once
#include <Arduino.h>

#if defined(ENABLE_DISPLAY) && (defined(BOARD_TECHO) || defined(BOARD_TBEAM))
#define HAS_DISPLAY 1
#else
#define HAS_DISPLAY 0
#endif

// An OLED costs one I2C frame to repaint, so it can follow the lamps live. An
// e-paper full refresh blocks for about two seconds, so it shows whatever the
// lamps were at the last status repaint and nothing makes it chase them.
#if HAS_DISPLAY && !defined(BOARD_TECHO)
#define DISPLAY_LIVE 1
#else
#define DISPLAY_LIVE 0
#endif

// What the radio and the links are doing, as a row of lamps on the screen.
//
// Not a set of independent booleans on the screen's side: they are read
// together once per loop and compared as a whole, so a repaint happens when
// the picture changes rather than once per flag.
struct Lamps {
    bool bleAdv;    // advertising, waiting for a host
    bool bleLink;   // a host connected over the UART service
    bool serial;    // a host sent a valid frame over the wire recently
    bool gps;       // a receiver is fitted to this board
    bool gpsFix;    // and it has a fix no older than the firmware's 30 s
    bool tx;        // transmitting
    bool rx;        // in receive, listening for packets
};

// Boards without a screen compile these away to nothing.
void displayInit();

// Redrawn at boot and whenever the configuration changes. E-paper refresh is
// slow and blocking, so nothing calls this on a per-packet basis.
void displayStatus(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power);

// Cheap to call from loop(). Repaints the last status only when the battery
// percentage has moved far enough to change what is on the glass.
void displayPoll();

// The lamp state, pushed every loop. Repaints only when it changed, and only
// where a repaint is cheap; elsewhere it is kept for the next status paint.
void displayLamps(const Lamps &lamps);

// Board-specific paint. Callers want displayStatus(), which also caches the
// arguments so displayPoll() can repaint without them.
void displayRender(const char *board, const char *radio, const char *fw,
                   const char *modem, float freqMhz, int8_t power,
                   const Lamps &lamps);
