#include "hal.h"
#include <stdarg.h>

void outf(const char *fmt, ...)
{
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) io.write((const uint8_t *)buf, (size_t)min((size_t)n, sizeof(buf) - 1));
}
