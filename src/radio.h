#pragma once
#include <RadioLib.h>

// The concrete radio object lives in main.cpp; protocol clients only need the
// PhysicalLayer interface, which keeps them free of the per-board typedefs.
PhysicalLayer *radioPhy();
