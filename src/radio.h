#pragma once
#include <RadioLib.h>

// The concrete radio object lives in main.cpp; protocol clients only need the
// PhysicalLayer interface, which keeps them free of the per-board typedefs.
PhysicalLayer *radioPhy();

// AX25Client::begin() calls startDirect(), which is right for AFSK but leaves
// the radio in continuous mode while sendFrame() wants an ordinary packet
// transmit. Rebuilding the packet config in between fixes that.
bool radioPacketMode();
