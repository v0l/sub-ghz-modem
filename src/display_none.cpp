#include "display.h"

#if !HAS_DISPLAY

void displayInit() {}
void displayStatus(const char *, const char *, const char *,
                   const char *, float, int8_t) {}
void displayPoll() {}
void displayRender(const char *, const char *, const char *,
                   const char *, float, int8_t) {}

#endif
